/*
 * Copyright (C) 2006-2009 Anders Brander <anders@brander.dk> and 
 * Anders Kvist <akv@lnxbx.dk>
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

#ifndef RAWSTUDIO_H
#define RAWSTUDIO_H

#ifdef  __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include "rs-types.h"

#include "rs-macros.h"

#include "rs-rawfile.h"
#include "rs-settings.h"
#include "rs-image.h"
#include "rs-image16.h"
#include "rs-metadata.h"
#include "rs-filetypes.h"
#include "rs-plugin.h"
#include "rs-filter.h"
#include "rs-output.h"
#include "rs-plugin-manager.h"
#include "rs-job-queue.h"
#include "rs-utils.h"
#include "rs-settings.h"
#include "rs-adobe-coeff.h"
#include "rs-color-transform.h"
#include "rs-spline.h"
#include "rs-curve.h"
#include "rs-stock.h"

#include "x86-cpu.h"

#ifdef  __cplusplus
} /* extern "c" */
#endif

#endif /* RAWSTUDIO_H */
