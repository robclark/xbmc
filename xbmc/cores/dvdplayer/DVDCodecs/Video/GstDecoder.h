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

class IGstDecoderCallback
{
public:
  virtual void OnDecodedBuffer(GstBuffer *buffer) = 0;
  virtual void OnNeedData() = 0;
  virtual void OnEnoughData() = 0;
};

class CGstDecoder : public CThread
{
public:
  CGstDecoder(IGstDecoderCallback *callback);
  ~CGstDecoder();

  GstElement *Open(GstCaps *sourceCapabilities);
  virtual void StopThread(bool bWait = true);

protected:
  virtual void Process();

private:
  static void OnDecodedBuffer(GstElement *appsink, void *data);
  static void OnNeedData(GstElement *appsrc, guint size, void *data);
  static void OnEnoughData (GstElement *appsrc, void *data);
  static gboolean BusCallback(GstBus *bus, GstMessage *msg, gpointer data);

  GstElement *m_pipeline;
  GMainLoop *m_loop;

  IGstDecoderCallback *m_callback;
};
