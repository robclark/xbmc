/*
 *      Copyright (C) 2005-2011 Team XBMC
 *      http://www.xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#include "GstDecoder.h"
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>

CGstDecoder::CGstDecoder(IGstDecoderCallback *callback) : m_callback(callback)
{
  m_loop      = NULL;
  m_pipeline  = NULL;
}

CGstDecoder::~CGstDecoder()
{
  if (m_pipeline)
  {
    gst_object_unref(m_pipeline);
    m_pipeline = NULL;
  }
}

GstElement *CGstDecoder::Open(GstCaps *sourceCapabilities)
{
  m_loop = g_main_loop_new (NULL, FALSE);
  if (m_loop == NULL)
    return false;

  gchar *capsString = gst_caps_to_string(sourceCapabilities);

  DBG("The capabilities from source are %s", capsString);

  gchar *pipelineString = g_strdup_printf(
      "appsrc caps=\"%s\" name=\"AppSrc\" stream-type=seekable format=time block=(boolean)true ! "
      "decodebin2 ! ffmpegcolorspace ! "
      "appsink caps=\"video/x-raw-yuv,format=(fourcc){I420,NV12}\" name=\"AppSink\" max-buffers=3",
      capsString);

  DBG("Entire pipeline is %s", pipelineString);

  m_pipeline = gst_parse_launch(pipelineString, NULL);
  g_free(capsString);
  g_free(pipelineString);

  if (m_pipeline == NULL)
    return NULL;

  GstBus *bus = gst_element_get_bus(m_pipeline);
  gst_bus_add_watch (bus, (GstBusFunc)BusCallback, this);
  gst_object_unref (bus);

  GstElement *AppSrc = gst_bin_get_by_name(GST_BIN(m_pipeline), "AppSrc");
  m_AppSrc = AppSrc;

  if (AppSrc)
  {
    g_signal_connect(AppSrc, "need-data", G_CALLBACK (OnNeedData), this);
    g_signal_connect(AppSrc, "enough-data", G_CALLBACK (OnEnoughData), this);
    g_signal_connect(AppSrc, "seek-data", G_CALLBACK (OnSeekData), this);
  }
  else
    ERR("Failure to hook up to AppSrc");

  GstElement *AppSink = gst_bin_get_by_name(GST_BIN(m_pipeline), "AppSink");
  m_AppSink = AppSink;

  if (AppSink)
  {
    g_object_set(G_OBJECT(AppSink), "emit-signals", TRUE, "sync", FALSE, NULL);
    g_signal_connect(AppSink, "crop", G_CALLBACK(CGstDecoder::OnCrop), this);
    g_signal_connect(AppSink, "new-buffer", G_CALLBACK(CGstDecoder::OnDecodedBuffer), this);
    gst_object_unref(AppSink);
  }
  else
    ERR("Failure to hook up to AppSink");

  Create();

  return AppSrc;
}

void CGstDecoder::StopThread(bool bWait)
{
  g_main_loop_quit(m_loop);
  CThread::StopThread(bWait);
}

void CGstDecoder::Reset(double dts, double pts)
{
  GstClockTime time = pts * 1000.0;
  DBG("Seeking to: %"GST_TIME_FORMAT, GST_TIME_ARGS(time));
  gst_element_seek(m_AppSrc, 1.0, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH,
      GST_SEEK_TYPE_SET, time, GST_SEEK_TYPE_NONE, 0);
}

void CGstDecoder::Process()
{
  gst_element_set_state(m_pipeline, GST_STATE_PLAYING);

  g_main_loop_run(m_loop);

  gst_element_set_state(m_pipeline, GST_STATE_NULL);
}

void CGstDecoder::OnCrop(GstElement *appsink, gint top, gint left, gint width, gint height, void *data)
{
  CGstDecoder *decoder = (CGstDecoder *)data;
  if (decoder->m_callback)
    decoder->m_callback->OnCrop(top, left, width, height);
}

void CGstDecoder::OnDecodedBuffer(GstElement *appsink, void *data)
{
  CGstDecoder *decoder = (CGstDecoder *)data;

  GstBuffer *buffer = gst_app_sink_pull_buffer(GST_APP_SINK(appsink));
  if (buffer)
  {
    DBG("got buffer: %"GST_TIME_FORMAT, GST_TIME_ARGS(GST_BUFFER_TIMESTAMP(buffer)));

    if (decoder->m_callback)
      decoder->m_callback->OnDecodedBuffer(buffer);
    else
      gst_buffer_unref(buffer);
  }
  else
    DBG("Null Buffer");
}

void CGstDecoder::OnNeedData(GstElement *appsrc, guint size, void *data)
{
  CGstDecoder *decoder = (CGstDecoder *)data;

  if (decoder->m_callback)
    decoder->m_callback->OnNeedData();
}

/* This callback is called when appsrc has enough data and we can stop sending.
 * We remove the idle handler from the mainloop */
void CGstDecoder::OnEnoughData (GstElement *appsrc, void *data)
{
  CGstDecoder *decoder = (CGstDecoder *)data;

  if (decoder->m_callback)
    decoder->m_callback->OnEnoughData();
}

gboolean CGstDecoder::OnSeekData(GstElement *appsrc, guint64 arg, void *data)
{
  return TRUE;
}

gboolean CGstDecoder::BusCallback(GstBus *bus, GstMessage *msg, gpointer data)
{
  CGstDecoder *decoder = (CGstDecoder *)data;
  gchar  *str;

  switch (GST_MESSAGE_TYPE(msg))
  {
    case GST_MESSAGE_EOS:
      DBG("End of stream");
      g_main_loop_quit(decoder->m_loop);
      break;

    case GST_MESSAGE_ERROR:
      GError *error;

      gst_message_parse_error (msg, &error, &str);
      g_free (str);

      ERR("Error - %s %s", str, error->message);
      g_error_free (error);

      g_main_loop_quit(decoder->m_loop);

      if (decoder->m_callback)
        decoder->m_callback->OnError(error->message);

      break;

    case GST_MESSAGE_WARNING:
      GError *warning;

      gst_message_parse_error (msg, &warning, &str);
      g_free (str);

      ERR("Warning - %s %s", str, warning->message);
      g_error_free (warning);
      break;

    case GST_MESSAGE_INFO:
      GError *info;

      gst_message_parse_error (msg, &info, &str);
      g_free (str);

      DBG("Info - %s %s", str, info->message);
      g_error_free (info);
      break;

    case GST_MESSAGE_STATE_CHANGED:
    {
      /* dump graph for pipeline state changes */
      GstState old_state, new_state;
      gchar *state_transition_name;

      gst_message_parse_state_changed (msg, &old_state, &new_state, NULL);

      state_transition_name = g_strdup_printf ("%s_%s",
          gst_element_state_get_name (old_state),
          gst_element_state_get_name (new_state));

      gchar *dump_name = g_strconcat ("xbmc.", state_transition_name, NULL);
      DBG("dumping: %s", dump_name);
      GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (decoder->m_pipeline),
          GST_DEBUG_GRAPH_SHOW_ALL, dump_name);
      DBG("done");

      g_free (dump_name);
      g_free (state_transition_name);

      break;
    }

    default:
      DBG("%"GST_PTR_FORMAT, msg);
      break;
  }

  return TRUE;
}
