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

#include "system.h"
#if (defined HAVE_CONFIG_H) && (!defined WIN32)
  #include "config.h"
#endif
#include "DVDVideoCodecGStreamer.h"
#include "DVDStreamInfo.h"
#include "DVDClock.h"
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>

bool CDVDVideoCodecGStreamer::gstinitialized = false;

CDVDVideoCodecGStreamer::CDVDVideoCodecGStreamer()
{
  if (gstinitialized == false)
 {
    gst_init (NULL, NULL);
    gstinitialized = true;
  }

  m_initialized = false;
  m_pictureBuffer = NULL;

  m_decoder = NULL;
  m_needData = false;
  m_AppSrc = NULL;
  m_AppSrcCaps = NULL;
  m_ptsinvalid = true;

  m_timebase = 1000.0;
}

CDVDVideoCodecGStreamer::~CDVDVideoCodecGStreamer()
{
  Dispose();
}

bool CDVDVideoCodecGStreamer::Open(CDVDStreamInfo &hints, CDVDCodecOptions &options)
{
  Dispose();

  m_ptsinvalid = hints.ptsinvalid;

  m_AppSrcCaps = CreateVideoCaps(hints, options);

  if (m_AppSrcCaps)
  {
    m_decoder = new CGstDecoder(this);
    m_AppSrc = m_decoder->Open(m_AppSrcCaps);
  }

  return (m_AppSrc != NULL);
}

void CDVDVideoCodecGStreamer::Dispose()
{
  while (m_pictureQueue.size())
  {
    gst_buffer_unref(m_pictureQueue.front());
    m_pictureQueue.pop();
  }

  if (m_pictureBuffer)
  {
    gst_buffer_unref(m_pictureBuffer);
    m_pictureBuffer = NULL;
  }

  if (m_AppSrc)
  {
    GstFlowReturn ret;
    g_signal_emit_by_name(m_AppSrc, "end-of-stream", &ret);

    if (ret != GST_FLOW_OK)
      printf("GStreamer: OnDispose. Flow error %i\n", ret);

    gst_object_unref(m_AppSrc);
    m_AppSrc = NULL;
  }

  if (m_AppSrcCaps)
  {
    gst_caps_unref(m_AppSrcCaps);
    m_AppSrcCaps = NULL;
  }

  if (m_decoder)
  {
    m_decoder->StopThread();
    delete m_decoder;
    m_decoder = NULL;

    m_initialized = false;
  }
}

int CDVDVideoCodecGStreamer::Decode(BYTE* pData, int iSize, double dts, double pts)
{
  CSingleLock lock(m_monitorLock);
  usleep(100);

  GstBuffer *buffer = NULL;

  if (pData)
  {
    buffer = gst_buffer_new_and_alloc(iSize);
    if (buffer)
    {
      memcpy(GST_BUFFER_DATA(buffer), pData, iSize);

      GST_BUFFER_TIMESTAMP(buffer) = pts * 1000.0;

      GstFlowReturn ret;
      g_signal_emit_by_name(m_AppSrc, "push-buffer", buffer, &ret);

      if (ret != GST_FLOW_OK)
        printf("GStreamer: OnDecode. Flow error %i\n", ret);

      gst_buffer_unref(buffer);
    }
  }

  if (m_pictureBuffer)
 {
    gst_buffer_unref(m_pictureBuffer);
    m_pictureBuffer = NULL;
  }

  if (m_pictureQueue.size())
    return VC_PICTURE;
  else
    return VC_BUFFER;
}

void CDVDVideoCodecGStreamer::Reset()
{
}

bool CDVDVideoCodecGStreamer::GetPicture(DVDVideoPicture* pDvdVideoPicture)
{
  CSingleLock lock(m_monitorLock);
  if (m_pictureQueue.size())
  {
    m_pictureBuffer = m_pictureQueue.front();
    m_pictureQueue.pop();
  }
  else
    return false;

  GstCaps *caps = gst_buffer_get_caps(m_pictureBuffer);
  if (caps == NULL)
  {
    printf("GStreamer: No caps on decoded buffer\n");
    return false;
  }

  GstStructure *structure = gst_caps_get_structure (caps, 0);
  int width = 0, height = 0;
  if (structure == NULL ||
      !gst_structure_get_int (structure, "width", (int *) &width) ||
      !gst_structure_get_int (structure, "height", (int *) &height))
  {
    printf("GStreamer: invalid caps on decoded buffer\n");
    return false;
  }

  pDvdVideoPicture->iDisplayWidth  = pDvdVideoPicture->iWidth  = width;
  pDvdVideoPicture->iDisplayHeight = pDvdVideoPicture->iHeight = height;

  pDvdVideoPicture->format = DVDVideoPicture::FMT_YUV420P;

#define ALIGN(x, n) (((x) + (n) - 1) & (~((n) - 1)))
  pDvdVideoPicture->data[0] = m_pictureBuffer->data;
  pDvdVideoPicture->iLineSize[0] = ALIGN (width, 4);
  pDvdVideoPicture->data[1] = pDvdVideoPicture->data[0] + pDvdVideoPicture->iLineSize[0] * ALIGN (height, 2);
  pDvdVideoPicture->iLineSize[1] = ALIGN (width, 8) / 2;
  pDvdVideoPicture->data[2] = pDvdVideoPicture->data[1] + pDvdVideoPicture->iLineSize[1] * ALIGN (height, 2) / 2;
  pDvdVideoPicture->iLineSize[2] = pDvdVideoPicture->iLineSize[1];
  g_assert (pDvdVideoPicture->data[2] + pDvdVideoPicture->iLineSize[2] * ALIGN (height, 2) / 2 == pDvdVideoPicture->data[0] + m_pictureBuffer->size);
#undef ALIGN

  pDvdVideoPicture->pts = (double)GST_BUFFER_TIMESTAMP(m_pictureBuffer) / 1000.0;
  pDvdVideoPicture->iDuration = (double)GST_BUFFER_DURATION(m_pictureBuffer) / 1000.0;

  return true;
}

void CDVDVideoCodecGStreamer::SetDropState(bool bDrop)
{
}

const char *CDVDVideoCodecGStreamer::GetName()
{
  return "GStreamer";
}

void CDVDVideoCodecGStreamer::OnDecodedBuffer(GstBuffer *buffer)
{
  if (buffer)
  {
    CSingleLock lock(m_monitorLock);
    m_pictureQueue.push(buffer);
  }
  else
    printf("GStreamer: Received null buffer?\n");
}

void CDVDVideoCodecGStreamer::OnNeedData()
{
  m_needData = true;
}

void CDVDVideoCodecGStreamer::OnEnoughData()
{
  m_needData = false;
}

GstCaps *CDVDVideoCodecGStreamer::CreateVideoCaps(CDVDStreamInfo &hints, CDVDCodecOptions &options)
{
  GstCaps *caps = NULL;

  switch (hints.codec)
  {
    case CODEC_ID_H264:
      caps = gst_caps_new_simple ("video/x-h264", NULL);
      break;

    default:
      printf("GStreamer: codec: unkown = %i\n", hints.codec);
      break;
  }

  if (caps)
  {
    gst_caps_set_simple(caps, 
                        "width", G_TYPE_INT, hints.width,
                        "height", G_TYPE_INT, hints.height,
                        "framerate", GST_TYPE_FRACTION,
                          (hints.vfr ? 0 : hints.fpsrate),
                          (hints.vfr ? 1 : hints.fpsscale),
                        NULL);

    if (hints.extradata && hints.extrasize > 0)
    {
      GstBuffer *data = NULL;
      data = gst_buffer_new_and_alloc(hints.extrasize);
      memcpy(GST_BUFFER_DATA(data), hints.extradata, hints.extrasize);

      gst_caps_set_simple(caps, "codec_data", GST_TYPE_BUFFER, data, NULL);
      gst_buffer_unref(data);
    }
  }

  return caps;
}
