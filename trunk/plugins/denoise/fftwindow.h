/*
* Copyright (C) 2009 Klaus Post
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License
* as published by the Free Software Foundation; either version 2
* of the License, or (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
#ifndef fftwindow_h__
#define fftwindow_h__
#include "floatimageplane.h"


class FFTWindow
{
public:
  FFTWindow(int _w, int _h);
  virtual ~FFTWindow(void);
  FloatImagePlane analysis;
  FloatImagePlane synthesis;
  void createHalfCosineWindow(int ox, int oy);
  void createRaisedCosineWindow(int ox, int oy);
  void createSqrtHalfCosineWindow(int ox, int oy);
  void applyAnalysisWindow(FloatImagePlane *image, FloatImagePlane *dst); 
  void applySynthesisWindow( FloatImagePlane *image ); // Inplace, written back to image
private:
  void createWindow( FloatImagePlane &window, int ox, float* wx);
  bool isFlat; 
};

#endif // fftwindow_h__
