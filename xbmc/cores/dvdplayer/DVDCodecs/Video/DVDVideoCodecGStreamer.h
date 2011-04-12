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

#include "DVDVideoCodec.h"
#include "threads/SingleLock.h"
#include <gst/gst.h>
#include <queue>
#include "threads/Thread.h"
#include "GstDecoder.h"

class CGstDecoder;

class CDVDVideoCodecGStreamer : public CDVDVideoCodec, public IGstDecoderCallback
{
public:
  CDVDVideoCodecGStreamer();
  virtual ~CDVDVideoCodecGStreamer();
  virtual bool Open(CDVDStreamInfo &hints, CDVDCodecOptions &options);
  virtual void Dispose();
  virtual int Decode(BYTE* pData, int iSize, double dts, double pts);
  virtual void Reset();
  virtual bool GetPicture(DVDVideoPicture* pDvdVideoPicture);
  virtual void SetDropState(bool bDrop);
  virtual const char* GetName();

  void OnDecodedBuffer(GstBuffer *buffer);
  void OnNeedData();
  void OnEnoughData();

private:
  static GstCaps *CreateVideoCaps(CDVDStreamInfo &hints, CDVDCodecOptions &options);

  static bool gstinitialized;

  bool m_initialized;

  std::queue<GstBuffer *> m_pictureQueue;
  GstBuffer *m_pictureBuffer;

  CCriticalSection m_needBuffer;
  CCriticalSection m_monitorLock;

  CGstDecoder *m_decoder;
  GstElement *m_AppSrc;
  GstCaps *m_AppSrcCaps;
  double m_timebase;

  bool m_needData;
  bool m_ptsinvalid;
};
