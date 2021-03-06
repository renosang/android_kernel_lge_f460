/*****************************************************************************
	Copyright(c) 2013 FCI Inc. All Rights Reserved

	File name : fc8080_tun.h

	Description : fc8080 tuner driver header file

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA


	History :
	----------------------------------------------------------------------
*******************************************************************************/

#ifndef __FC8080_TUNER__
#define __FC8080_TUNER__

#ifdef __cplusplus
extern "C" {
#endif

extern fci_s32 fc8080_tuner_init(HANDLE handle, enum band_type band);
extern fci_s32 fc8080_set_freq(HANDLE handle, enum band_type band,
								fci_u32 freq);
extern fci_s32 fc8080_get_rssi(HANDLE handle, fci_s32 *rssi);
extern fci_s32 fc8080_tuner_deinit(HANDLE handle);

#ifdef __cplusplus
}
#endif

#endif /* __FC8080_TUNER__ */

