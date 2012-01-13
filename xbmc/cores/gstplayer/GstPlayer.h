/*
 *      Copyright (C) 2012 linaro
 *      http://www.linaro.org
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

#pragma once
#include "cores/IPlayer.h"
#include "threads/Thread.h"
#include <gst/gst.h>

typedef struct{
   gchar * codec;
   gchar * lang;
   guint bitrate;
} MediaAudioInfo;

typedef struct {
   gchar * codec;
   guint bitrate;
} MediaVideoInfo;

typedef struct{
   bool loaded;
   gint video_num;
   gint audio_num;
   gint sub_num;
   gchar * container_format;
   gchar * title;
   gchar * genre;
   gchar * artist;
   gchar * description;
   gchar * album;
   gboolean seekable;
   MediaAudioInfo * audio_info;
   MediaVideoInfo * video_info;
} MediaInfo;

class CGstPlayer : public IPlayer, public CThread
{
public:
  CGstPlayer(IPlayerCallback& callback);
  virtual ~CGstPlayer();
  virtual void RegisterAudioCallback(IAudioCallback* pCallback) {}
  virtual void UnRegisterAudioCallback()                        {}
  virtual bool OpenFile(const CFileItem& file, const CPlayerOptions &options);
  virtual bool CloseFile();
  virtual bool IsPlaying() const;
  virtual void Pause();
  virtual bool IsPaused() const;
  virtual bool HasVideo() const;
  virtual bool HasAudio() const;
  virtual void ToggleOSD() { }; // empty
  virtual void SwitchToNextLanguage();
  virtual void ToggleSubtitles();
  virtual bool CanSeek();
  virtual void Seek(bool bPlus, bool bLargeStep);
  virtual void SeekPercentage(float iPercent);
  virtual float GetPercentage();
  virtual void SetVolume(long nVolume);
  virtual void SetDynamicRangeCompression(long drc) {}
  virtual void SetContrast(bool bPlus) {}
  virtual void SetBrightness(bool bPlus) {}
  virtual void SetHue(bool bPlus) {}
  virtual void SetSaturation(bool bPlus) {}
  virtual void GetAudioInfo(CStdString& strAudioInfo);
  virtual void GetVideoInfo(CStdString& strVideoInfo);
  virtual void GetGeneralInfo( CStdString& strVideoInfo);
  virtual void Update(bool bPauseDrawing)                       {}
  virtual void SwitchToNextAudioLanguage();
  virtual bool CanRecord() { return false; }
  virtual bool IsRecording() { return false; }
  virtual bool Record(bool bOnOff) { return false; }
  virtual void SetAVDelay(float fValue = 0.0f);
  virtual float GetAVDelay();
  virtual bool OnAction(const CAction &action);
  virtual void GetVideoRect(CRect& SrcRect, CRect& DestRect);

  virtual void SetSubTitleDelay(float fValue = 0.0f);
  virtual float GetSubTitleDelay();

  virtual void SeekTime(__int64 iTime);
  virtual __int64 GetTime();
  virtual int GetTotalTime();
  virtual void ToFFRW(int iSpeed);
  virtual void DoAudioWork()                                    {}
  
  virtual CStdString GetPlayerState();
  virtual bool SetPlayerState(CStdString state);

  
  virtual int GetCacheLevel() const ;
  virtual bool IsCaching() const { return m_buffering == TRUE; }
  virtual bool LoadMediaInfo();

  virtual int GetAudioStreamCount();
  virtual void GetAudioStreamName(int iStream, CStdString &strStreamName);
  virtual void SetAudioStream(int iStream);
  virtual int GetAudioStream();

  
private:
  virtual void Process();
  bool CreatePlayBin();
  bool DestroyPlayBin();
  void SetPlaybackRate(int iSpeed, gint64 pos);
  CStdString ParseAndCorrectUrl(CURL &url);
  void ResetUrlInfo();
  void SetSinkRenderDelay(GstElement * ele, guint64 renderDelay);
  void CleanMediaInfo();
  int OutputPicture(GstBuffer * gstbuffer);
  bool SetAndWaitPlaybinState(GstState newstate, int timeout);

  CFileItem   m_item;

  CRect m_srcRect;
  CRect m_destRect;
  static void OnDecodedBuffer(GstElement *appsink, void *data);

  bool m_buffering;
  gint m_cache_level;

  bool m_paused;
  bool m_quit_msg;
  __int64 m_clock;
  DWORD m_lastTime;
  bool m_voutput_init;
  int m_speed;
  bool m_cancelled;
  gint64 m_starttime;
  CEvent m_ready;

  MediaInfo m_mediainfo;
  GstBus     *m_bus;
  GstElement *m_playbin;

  int m_file_cnt;

  int m_audio_current;
  int m_video_current;

  struct SOutputConfiguration
  {
    unsigned int width;
    unsigned int height;
    unsigned int dwidth;
    unsigned int dheight;
    unsigned int color_format;
    unsigned int extended_format;
    unsigned int color_matrix : 4;
    unsigned int color_range  : 1;
    unsigned int chroma_position;
    unsigned int color_primaries;
    unsigned int color_transfer;
    double       framerate;
  } m_output; 

  CStdString      m_username;
  CStdString      m_password;
  std::map<CStdString, CStdString> m_requestheaders;
};
