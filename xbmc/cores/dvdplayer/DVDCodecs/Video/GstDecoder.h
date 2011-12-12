#pragma once
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

#include <gst/gst.h>
#include <queue>
#include "threads/Thread.h"

static inline int debug_enabled(void)
{
  static int enabled = -1;
  if (enabled == -1)
  {
    char *str = getenv("XBMC_DEBUG");
    enabled = str && strstr(str, "decode");
  }
  return enabled;
}

#define DBG(fmt, ...) do { \
    if (debug_enabled()) \
		printf("%"GST_TIME_FORMAT"\t%s:%d\t"fmt"\n", \
		    GST_TIME_ARGS(gst_util_get_timestamp()), \
		    __PRETTY_FUNCTION__, __LINE__, ##__VA_ARGS__); \
  } while (0)

#define ERR(fmt, ...) do { \
    printf("%"GST_TIME_FORMAT"\t%s:%d\tERROR: "fmt"\n", \
        GST_TIME_ARGS(gst_util_get_timestamp()), \
        __PRETTY_FUNCTION__, __LINE__, ##__VA_ARGS__); \
  } while (0)

class IGstDecoderCallback
{
public:
  virtual void OnCrop(gint top, gint left, gint width, gint height) = 0;
  virtual void OnDecodedBuffer(GstBuffer *buffer) = 0;
  virtual void OnNeedData() = 0;
  virtual void OnEnoughData() = 0;
  virtual void OnError(const gchar *message) = 0;
};

class CGstDecoder : public CThread
{
public:
  CGstDecoder(IGstDecoderCallback *callback);
  ~CGstDecoder();

  GstElement *Open(GstCaps *sourceCapabilities);
  virtual void StopThread(bool bWait = true);
  virtual void Reset(double dts, double pts);

protected:
  virtual void Process();

private:
  static void OnCrop(GstElement *appsink, gint top, gint left, gint width, gint height, void *data);
  static void OnDecodedBuffer(GstElement *appsink, void *data);
  static void OnNeedData(GstElement *appsrc, guint size, void *data);
  static void OnEnoughData(GstElement *appsrc, void *data);
  static gboolean OnSeekData(GstElement *appsrc, guint64 arg, void *data);
  static gboolean BusCallback(GstBus *bus, GstMessage *msg, gpointer data);

  GstElement *m_pipeline;
  GMainLoop *m_loop;
  GstElement *m_AppSrc, *m_AppSink;

  IGstDecoderCallback *m_callback;
};
