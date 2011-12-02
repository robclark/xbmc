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
#include "windowing/WindowingFactory.h"
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include <EGL/egl.h>

bool CDVDVideoCodecGStreamer::gstinitialized = false;


#ifndef EGLIMAGE_FLAGS_YUV_CONFORMANT_RANGE
// XXX these should come from some egl header??
#define EGLIMAGE_FLAGS_YUV_CONFORMANT_RANGE (0 << 0)
#define EGLIMAGE_FLAGS_YUV_FULL_RANGE       (1 << 0)
#define EGLIMAGE_FLAGS_YUV_BT601            (0 << 1)
#define EGLIMAGE_FLAGS_YUV_BT709            (1 << 1)
#endif
#ifndef EGL_TI_raw_video
#  define EGL_TI_raw_video 1
#  define EGL_RAW_VIDEO_TI            0x333A  /* eglCreateImageKHR target */
#  define EGL_GL_VIDEO_FOURCC_TI        0x3331  /* eglCreateImageKHR attribute */
#  define EGL_GL_VIDEO_WIDTH_TI         0x3332  /* eglCreateImageKHR attribute */
#  define EGL_GL_VIDEO_HEIGHT_TI        0x3333  /* eglCreateImageKHR attribute */
#  define EGL_GL_VIDEO_BYTE_STRIDE_TI     0x3334  /* eglCreateImageKHR attribute */
#  define EGL_GL_VIDEO_BYTE_SIZE_TI       0x3335  /* eglCreateImageKHR attribute */
#  define EGL_GL_VIDEO_YUV_FLAGS_TI       0x3336  /* eglCreateImageKHR attribute */
#endif


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
  m_AppSinkCaps = NULL;
  m_ptsinvalid = true;

  m_timebase = 1000.0;

  eglCreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC)
      eglGetProcAddress("eglCreateImageKHR");
  eglDestroyImageKHR = (PFNEGLDESTROYIMAGEKHRPROC)
      eglGetProcAddress("eglDestroyImageKHR");

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

  if (m_AppSinkCaps)
  {
    gst_caps_unref(m_AppSinkCaps);
    m_AppSinkCaps = NULL;
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
  m_crop = false;
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

  if (caps != m_AppSinkCaps)
  {
    if (m_AppSinkCaps)
      gst_caps_unref(m_AppSinkCaps);

    m_AppSinkCaps = caps;

    GstStructure *structure = gst_caps_get_structure (caps, 0);
    if (structure == NULL ||
        !gst_structure_get_int (structure, "width", &m_width) ||
        !gst_structure_get_int (structure, "height", &m_height) ||
        !gst_structure_get_fourcc (structure, "format", &m_format))
    {
      printf("GStreamer: invalid caps on decoded buffer\n");
      gst_caps_unref(m_AppSinkCaps);
      m_AppSinkCaps = NULL;
      return false;
    }

    /* we could probably even lift this restriction on color formats
     * (note: update caps filter in gst pipeline if you do).. this
     * might make sense on OMAP3 where DSP codecs might be returning
     * YUY2/UYVY.. at least if we are using eglimageexternal/
     * texture streaming, the SGX can directly render YUY2/UYVY
     *
     * XXX if using eglImage, we need some way to query supported YUV
     * formats..
     */
    if ((m_format != GST_STR_FOURCC("NV12")) &&
        (m_format != GST_STR_FOURCC("I420")))
    {
      printf("GStreamer: invalid color format on decoded buffer\n");
      gst_caps_unref(m_AppSinkCaps);
      m_AppSinkCaps = NULL;
      return false;
    }
  }
  else
  {
    gst_caps_unref(caps);
  }

  pDvdVideoPicture->iWidth  = m_width;
  pDvdVideoPicture->iHeight = m_height;

  if (m_crop)
  {
    pDvdVideoPicture->iDisplayWidth  = m_cropWidth;
    pDvdVideoPicture->iDisplayHeight = m_cropHeight;
    pDvdVideoPicture->iDisplayX      = m_cropLeft;
    pDvdVideoPicture->iDisplayY      = m_cropTop;
  }
  else
  {
    pDvdVideoPicture->iDisplayWidth  = pDvdVideoPicture->iWidth;
    pDvdVideoPicture->iDisplayHeight = pDvdVideoPicture->iHeight;
    pDvdVideoPicture->iDisplayX      = 0;
    pDvdVideoPicture->iDisplayY      = 0;
  }

  pDvdVideoPicture->decoder = this;
  pDvdVideoPicture->origBuf = m_pictureBuffer;
  pDvdVideoPicture->format  = DVDVideoPicture::FMT_EGLIMG;
  pDvdVideoPicture->pts       = (double)GST_BUFFER_TIMESTAMP(m_pictureBuffer) / 1000.0;
  pDvdVideoPicture->iDuration = (double)GST_BUFFER_DURATION(m_pictureBuffer) / 1000.0;

  return true;
}

EGLImageKHR CDVDVideoCodecGStreamer::GetEGLImage(void *origBuf)
{
  GstBuffer *buf = gst_buffer_ref((GstBuffer *)origBuf);
  EGLint attr[] = {
      EGL_GL_VIDEO_FOURCC_TI,      m_format,
      EGL_GL_VIDEO_WIDTH_TI,       m_width,
      EGL_GL_VIDEO_HEIGHT_TI,      m_height,
      EGL_GL_VIDEO_BYTE_SIZE_TI,   GST_BUFFER_SIZE(buf),
      // TODO: pick proper YUV flags..
      EGL_GL_VIDEO_YUV_FLAGS_TI,   EGLIMAGE_FLAGS_YUV_CONFORMANT_RANGE |
                                   EGLIMAGE_FLAGS_YUV_BT601,
      EGL_NONE
  };
  return eglCreateImageKHR(g_Windowing.GetEGLDisplay(),
      EGL_NO_CONTEXT, EGL_RAW_VIDEO_TI, GST_BUFFER_DATA(buf), attr);
}

void CDVDVideoCodecGStreamer::ReleaseEGLImage(EGLImageKHR eglImage, void *origBuf)
{
  GstBuffer *buf = (GstBuffer *)origBuf;
  eglDestroyImageKHR(g_Windowing.GetEGLDisplay(), eglImage);
  gst_buffer_unref(buf);
}

void CDVDVideoCodecGStreamer::SetDropState(bool bDrop)
{
}

const char *CDVDVideoCodecGStreamer::GetName()
{
  return "GStreamer";
}

void CDVDVideoCodecGStreamer::OnCrop(gint top, gint left, gint width, gint height)
{
  m_crop = true;
  m_cropTop = top;
  m_cropLeft = left;
  m_cropWidth = width;
  m_cropHeight = height;
}

void CDVDVideoCodecGStreamer::OnDecodedBuffer(GstBuffer *buffer)
{
  /* throttle decoding if rendering is not keeping up.. */
  while (m_pictureQueue.size() > 4)
    usleep(1000);

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
