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

static PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR;
static PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR;


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

class GSTEGLImageHandle : public EGLImageHandle
{
public:
  GSTEGLImageHandle(GstBuffer *buf, gint width, gint height, guint32 format)
    : EGLImageHandle()
  {
    this->eglImage = NULL;
    this->buf = buf;
    this->width = width;
    this->height = height;
    this->format = format;
    this->refcnt = 1;
  }

  virtual ~GSTEGLImageHandle()
  {
    if (eglImage)
      eglDestroyImageKHR(g_Windowing.GetEGLDisplay(), eglImage);
    gst_buffer_unref(buf);
  }

  virtual EGLImageHandle * Ref()
  {
    CSingleLock lock(m_monitorLock);
    refcnt++;
    return this;
  }

  void UnRef()
  {
    CSingleLock lock(m_monitorLock);
    --refcnt;
    if (refcnt == 0)
      delete this;
  }

  virtual EGLImageKHR Get()
  {
    if (!eglImage)
    {
      EGLint attr[] = {
          EGL_GL_VIDEO_FOURCC_TI,      format,
          EGL_GL_VIDEO_WIDTH_TI,       width,
          EGL_GL_VIDEO_HEIGHT_TI,      height,
          EGL_GL_VIDEO_BYTE_SIZE_TI,   GST_BUFFER_SIZE(buf),
          // TODO: pick proper YUV flags..
          EGL_GL_VIDEO_YUV_FLAGS_TI,   EGLIMAGE_FLAGS_YUV_CONFORMANT_RANGE |
          EGLIMAGE_FLAGS_YUV_BT601,
          EGL_NONE
      };
      eglImage = eglCreateImageKHR(g_Windowing.GetEGLDisplay(),
          EGL_NO_CONTEXT, EGL_RAW_VIDEO_TI, GST_BUFFER_DATA(buf), attr);
    }
    return eglImage;
  }

private:
  static CCriticalSection m_monitorLock;
  int refcnt;
  EGLImageKHR eglImage;
  gint width, height;
  guint32 format;
  GstBuffer *buf;
};
CCriticalSection GSTEGLImageHandle::m_monitorLock;


CDVDVideoCodecGStreamer::CDVDVideoCodecGStreamer()
{
  if (gstinitialized == false)
 {
    gst_init (NULL, NULL);
    gstinitialized = true;

    eglCreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC)
        eglGetProcAddress("eglCreateImageKHR");
    eglDestroyImageKHR = (PFNEGLDESTROYIMAGEKHRPROC)
        eglGetProcAddress("eglDestroyImageKHR");
  }

  m_initialized = false;

  m_decoder = NULL;
  m_needData = true;
  m_AppSrc = NULL;
  m_AppSrcCaps = NULL;
  m_AppSinkCaps = NULL;
  m_ptsinvalid = true;
  m_drop = false;
  m_reset = false;
  m_error = false;
  m_crop = false;

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
  m_drop = false;
  m_reset = false;
  m_error = false;
  m_crop = false;

  m_AppSrcCaps = CreateVideoCaps(hints, options);

  if (m_AppSrcCaps)
  {
    m_decoder = new CGstDecoder(this);
    m_AppSrc = m_decoder->Open(m_AppSrcCaps);
  }

  return (m_AppSrc != NULL);
}

void CDVDVideoCodecGStreamer::Flush()
{
  while (m_pictureQueue.size())
  {
    gst_buffer_unref(m_pictureQueue.front());
    m_pictureQueue.pop();
  }
}

void CDVDVideoCodecGStreamer::Dispose()
{
  Flush();

  if (m_AppSrc)
  {
    GstFlowReturn ret;
    g_signal_emit_by_name(m_AppSrc, "end-of-stream", &ret);

    if (ret != GST_FLOW_OK)
      ERR("Flow error %i", ret);

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
  while (pData && !(m_needData||m_reset||m_error))
    usleep(1000);

  CSingleLock lock(m_monitorLock);

  GstBuffer *buffer = NULL;

  if (m_error)
  {
    m_error = false;
    return VC_ERROR;
  }

  if (pts == DVD_NOPTS_VALUE)
    pts = dts;

  if (pData)
  {
    if (m_reset)
    {
      m_decoder->Reset(dts, pts);
      Flush();
      m_reset = false;
    }

    buffer = gst_buffer_new_and_alloc(iSize);
    if (buffer)
    {
      memcpy(GST_BUFFER_DATA(buffer), pData, iSize);

      GST_BUFFER_TIMESTAMP(buffer) = pts * 1000.0;

      DBG("push buffer: %"GST_TIME_FORMAT", dts=%f, pts=%f",
          GST_TIME_ARGS(GST_BUFFER_TIMESTAMP(buffer)), dts, pts);

      GstFlowReturn ret;
      g_signal_emit_by_name(m_AppSrc, "push-buffer", buffer, &ret);

      if (ret != GST_FLOW_OK)
        ERR("Flow error %i", ret);

      gst_buffer_unref(buffer);
    }
  }

  if (m_pictureQueue.size())
    return VC_PICTURE;
  else
    return VC_BUFFER;
}

void CDVDVideoCodecGStreamer::Reset()
{
  m_reset = true;
}

bool CDVDVideoCodecGStreamer::GetPicture(DVDVideoPicture* pDvdVideoPicture)
{
  CSingleLock lock(m_monitorLock);
  GstBuffer *buf;
  if (m_pictureQueue.size())
  {
    buf = m_pictureQueue.front();
    m_pictureQueue.pop();
  }
  else
    return false;

  GstCaps *caps = gst_buffer_get_caps(buf);
  if (caps == NULL)
  {
    ERR("No caps on decoded buffer");
    gst_buffer_unref(buf);
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
      ERR("invalid caps on decoded buffer");
      gst_caps_unref(m_AppSinkCaps);
      m_AppSinkCaps = NULL;
      gst_buffer_unref(buf);
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
      ERR("invalid color format on decoded buffer");
      gst_caps_unref(m_AppSinkCaps);
      m_AppSinkCaps = NULL;
      gst_buffer_unref(buf);
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

  pDvdVideoPicture->eglImageHandle = new GSTEGLImageHandle(buf, m_width, m_height, m_format);
  pDvdVideoPicture->format  = DVDVideoPicture::FMT_EGLIMG;
  pDvdVideoPicture->pts     = (double)GST_BUFFER_TIMESTAMP(buf) / 1000.0;
  pDvdVideoPicture->dts     = DVD_NOPTS_VALUE;
  pDvdVideoPicture->iDuration = (double)GST_BUFFER_DURATION(buf) / 1000.0;

  DBG("create %p (%f)", pDvdVideoPicture->eglImageHandle, pDvdVideoPicture->pts);

  return true;
}

void CDVDVideoCodecGStreamer::SetDropState(bool bDrop)
{
  m_drop = bDrop;
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
  if (m_drop || m_reset)
  {
    DBG("dropping! drop=%d, reset=%d", m_drop, m_reset);
    gst_buffer_unref (buffer);
    return;
  }

  if (buffer)
  {
    CSingleLock lock(m_monitorLock);
    m_pictureQueue.push(buffer);
  }
  else
    ERR("Received null buffer?");
}

void CDVDVideoCodecGStreamer::OnNeedData()
{
  DBG("need-data");
  m_needData = true;
}

void CDVDVideoCodecGStreamer::OnEnoughData()
{
  DBG("enough-data");
  m_needData = false;
}

void CDVDVideoCodecGStreamer::OnError(const gchar *message)
{
  m_error = true;
}

GstCaps *CDVDVideoCodecGStreamer::CreateVideoCaps(CDVDStreamInfo &hints, CDVDCodecOptions &options)
{
  GstCaps *caps = NULL;
  int version = 0;

  switch (hints.codec)
  {
    case CODEC_ID_H264:
      caps = gst_caps_new_simple ("video/x-h264", NULL);
      break;
    case CODEC_ID_MPEG4:
      version++;
      version++;
    case CODEC_ID_MPEG2VIDEO:
      version++;
    case CODEC_ID_MPEG1VIDEO:
      version++;
      caps = gst_caps_new_simple ("video/mpeg", NULL);
      if (caps)
      {
        gst_caps_set_simple(caps,
            "mpegversion", G_TYPE_INT, version,
            "systemstream", G_TYPE_BOOLEAN, false,
            "parsed", G_TYPE_BOOLEAN, true,
            NULL);
      }
      break;
    case CODEC_ID_VP6:
      caps = gst_caps_new_simple ("video/x-vp6", NULL);
      break;
    case CODEC_ID_VP6F:
      caps = gst_caps_new_simple ("video/x-vp6-flash", NULL);
      break;
    default:
      ERR("codec: unknown = %i", hints.codec);
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

  DBG("got caps: %"GST_PTR_FORMAT, caps);

  return caps;
}
