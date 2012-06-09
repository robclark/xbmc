/*
 *      Copyright (C) 2010 Team XBMC
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

/*
 * The texture object uses the GL_TEXTURE_EXTERNAL_OES texture target, which
 * is defined by the GL_OES_EGL_image_external OpenGL ES extension. This
 * limits how the texture may be used. Each time the texture is bound it
 * must be bound to the GL_TEXTURE_EXTERNAL_OES target rather than the
 * GL_TEXTURE_2D target. Additionally, any OpenGL ES 2.0 shader that samples
 * from the texture must declare its use of this extension using, for example,
 * an "#extension GL_OES_EGL_image_external : require" directive. Such shaders
 * must also access the texture using the samplerExternalOES GLSL sampler type.
 */

#extension GL_OES_EGL_image_external : require

precision mediump float;

uniform samplerExternalOES m_samp;
varying vec2   m_cord;
uniform float  m_alpha;

void main()
{
  vec4 rgb = texture2D(m_samp, m_cord);
  rgb.a = m_alpha;
  gl_FragColor = rgb;
}
