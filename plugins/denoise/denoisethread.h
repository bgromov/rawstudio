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

#ifndef denoisethread_h__
#define denoisethread_h__

#include "fftw3.h"
#include "jobqueue.h"
#include "pthread.h"
#include "complexblock.h"
#include "floatimageplane.h"

class DenoiseThread
{
public:
  DenoiseThread(void);
  virtual ~DenoiseThread(void);
  void addJobs(JobQueue *waiting, JobQueue *finished);
  void runDenoise();
  fftwf_plan forward;
  fftwf_plan reverse;
  ComplexBlock *complex;
  FloatImagePlane *input_plane;
  pthread_t thread_id;
  pthread_cond_t run_thread;
  pthread_mutex_t run_thread_mutex;
  gboolean exitThread;
  gboolean threadExited;
private:
  JobQueue *waiting;
  JobQueue *finished;

};
#endif // denoisethread_h__
