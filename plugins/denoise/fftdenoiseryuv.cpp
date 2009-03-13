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
#include "fftdenoiseryuv.h"

FFTDenoiserYUV::FFTDenoiserYUV(void)
{
}

FFTDenoiserYUV::~FFTDenoiserYUV(void)
{
}

void FFTDenoiserYUV::denoiseImage( RS_IMAGE16* image )
{
  FloatPlanarImage img;
  img.bw = FFT_BLOCK_SIZE;
  img.bh = FFT_BLOCK_SIZE;
  img.ox = FFT_BLOCK_OVERLAP;
  img.oy = FFT_BLOCK_OVERLAP;

  g_assert(image->channels == 3);

  img.unpackInterleaved_RGB_YUV(image);

  if (abort) return;

  img.mirrorEdges();
  if (abort) return;

  FFTWindow window(img.bw,img.bh);
  window.createHalfCosineWindow(img.ox, img.oy);

  ComplexFilter *filter = new ComplexWienerFilterDeGrid(img.bw, img.bh, beta, sigmaLuma, 1.0, plan_forward, &window);
  filter->setSharpen(sharpen, sharpenMinSigma, sharpenMaxSigma, sharpenCutoff);
  img.setFilter(0,filter,&window);

  filter = new ComplexWienerFilterDeGrid(img.bw, img.bh, beta, sigmaChroma, 1.0, plan_forward, &window);
  filter->setSharpen(sharpenChroma, sharpenMinSigmaChroma, sharpenMaxSigmaChroma, sharpenCutoffChroma);
  img.setFilter(1,filter,&window);

  filter = new ComplexWienerFilterDeGrid(img.bw, img.bh, beta, sigmaChroma, 1.0, plan_forward, &window);
  filter->setSharpen(sharpenChroma, sharpenMinSigmaChroma, sharpenMaxSigmaChroma, sharpenCutoffChroma);
  img.setFilter(2,filter,&window);

  FloatPlanarImage outImg(img);

  processJobs(img, outImg);
  if (abort) return;

  // Convert back
  outImg.packInterleaved_YUV_RGB(image);
}


void FFTDenoiserYUV::setParameters( FFTDenoiseInfo *info )
{
  FFTDenoiser::setParameters(info);
  sigmaLuma = info->sigmaLuma*SIGMA_FACTOR;
  sigmaChroma = info->sigmaChroma*SIGMA_FACTOR;
  sharpen = info->sharpenLuma;
  sharpenCutoff = info->sharpenCutoffLuma;
  sharpenMinSigma = info->sharpenMinSigmaLuma*SIGMA_FACTOR;
  sharpenMaxSigma = info->sharpenMaxSigmaLuma*SIGMA_FACTOR;
  sharpenChroma = info->sharpenChroma;
  sharpenCutoffChroma = info->sharpenCutoffChroma;
  sharpenMinSigmaChroma = info->sharpenMinSigmaChroma*SIGMA_FACTOR;
  sharpenMaxSigmaChroma = info->sharpenMaxSigmaChroma*SIGMA_FACTOR;
}