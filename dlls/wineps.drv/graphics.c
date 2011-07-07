/*
 *	PostScript driver graphics functions
 *
 *	Copyright 1998  Huw D M Davies
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <math.h>
#if defined(HAVE_FLOAT_H)
# include <float.h>
#endif
#if !defined(PI)
# define PI M_PI
#endif
#include "psdrv.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(psdrv);

/***********************************************************************
 *           PSDRV_XWStoDS
 *
 * Performs a world-to-viewport transformation on the specified width.
 */
INT PSDRV_XWStoDS( PHYSDEV dev, INT width )
{
    POINT pt[2];

    pt[0].x = 0;
    pt[0].y = 0;
    pt[1].x = width;
    pt[1].y = 0;
    LPtoDP( dev->hdc, pt, 2 );
    return pt[1].x - pt[0].x;
}

/***********************************************************************
 *           PSDRV_DrawLine
 */
static void PSDRV_DrawLine( PHYSDEV dev )
{
    PSDRV_PDEVICE *physDev = get_psdrv_dev( dev );

    if(physDev->pathdepth)
        return;

    if (physDev->pen.style == PS_NULL)
	PSDRV_WriteNewPath(dev);
    else
	PSDRV_WriteStroke(dev);
}

/***********************************************************************
 *           PSDRV_LineTo
 */
BOOL CDECL PSDRV_LineTo(PHYSDEV dev, INT x, INT y)
{
    POINT pt[2];

    TRACE("%d %d\n", x, y);

    GetCurrentPositionEx( dev->hdc, pt );
    pt[1].x = x;
    pt[1].y = y;
    LPtoDP( dev->hdc, pt, 2 );

    PSDRV_SetPen(dev);

    PSDRV_SetClip(dev);
    PSDRV_WriteMoveTo(dev, pt[0].x, pt[0].y );
    PSDRV_WriteLineTo(dev, pt[1].x, pt[1].y );
    PSDRV_DrawLine(dev);
    PSDRV_ResetClip(dev);

    return TRUE;
}


/***********************************************************************
 *           PSDRV_Rectangle
 */
BOOL CDECL PSDRV_Rectangle( PHYSDEV dev, INT left, INT top, INT right, INT bottom )
{
    PSDRV_PDEVICE *physDev = get_psdrv_dev( dev );
    RECT rect;

    TRACE("%d %d - %d %d\n", left, top, right, bottom);

    rect.left = left;
    rect.top = top;
    rect.right = right;
    rect.bottom = bottom;
    LPtoDP( dev->hdc, (POINT *)&rect, 2 );

    /* Windows does something truly hacky here.  If we're in passthrough mode
       and our rop is R2_NOP, then we output the string below.  This is used in
       Office 2k when inserting eps files */
    if(physDev->job.in_passthrough && !physDev->job.had_passthrough_rect && GetROP2(dev->hdc) == R2_NOP) {
      char buf[256];
      sprintf(buf, "N %d %d %d %d B\n", rect.right - rect.left, rect.bottom - rect.top, rect.left, rect.top);
      write_spool(dev, buf, strlen(buf));
      physDev->job.had_passthrough_rect = TRUE;
      return TRUE;
    }

    PSDRV_SetPen(dev);

    PSDRV_SetClip(dev);
    PSDRV_WriteRectangle(dev, rect.left, rect.top, rect.right - rect.left,
                         rect.bottom - rect.top );
    PSDRV_Brush(dev,0);
    PSDRV_DrawLine(dev);
    PSDRV_ResetClip(dev);
    return TRUE;
}


/***********************************************************************
 *           PSDRV_RoundRect
 */
BOOL CDECL PSDRV_RoundRect( PHYSDEV dev, INT left, INT top, INT right,
                            INT bottom, INT ell_width, INT ell_height )
{
    RECT rect[2];

    rect[0].left   = left;
    rect[0].top    = top;
    rect[0].right  = right;
    rect[0].bottom = bottom;
    rect[1].left   = 0;
    rect[1].top    = 0;
    rect[1].right  = ell_width;
    rect[1].bottom = ell_height;
    LPtoDP( dev->hdc, (POINT *)rect, 4 );

    left   = rect[0].left;
    top    = rect[0].top;
    right  = rect[0].right;
    bottom = rect[0].bottom;
    if (left > right) { INT tmp = left; left = right; right = tmp; }
    if (top > bottom) { INT tmp = top; top = bottom; bottom = tmp; }

    ell_width  = rect[1].right - rect[1].left;
    ell_height = rect[1].bottom - rect[1].top;
    if (ell_width > right - left) ell_width = right - left;
    if (ell_height > bottom - top) ell_height = bottom - top;

    PSDRV_WriteSpool(dev, "%RoundRect\n",11);
    PSDRV_SetPen(dev);

    PSDRV_SetClip(dev);
    PSDRV_WriteMoveTo( dev, left, top + ell_height/2 );
    PSDRV_WriteArc( dev, left + ell_width/2, top + ell_height/2, ell_width,
		    ell_height, 90.0, 180.0);
    PSDRV_WriteLineTo( dev, right - ell_width/2, top );
    PSDRV_WriteArc( dev, right - ell_width/2, top + ell_height/2, ell_width,
		    ell_height, 0.0, 90.0);
    PSDRV_WriteLineTo( dev, right, bottom - ell_height/2 );
    PSDRV_WriteArc( dev, right - ell_width/2, bottom - ell_height/2, ell_width,
		    ell_height, -90.0, 0.0);
    PSDRV_WriteLineTo( dev, right - ell_width/2, bottom);
    PSDRV_WriteArc( dev, left + ell_width/2, bottom - ell_height/2, ell_width,
		    ell_height, 180.0, -90.0);
    PSDRV_WriteClosePath( dev );

    PSDRV_Brush(dev,0);
    PSDRV_DrawLine(dev);
    PSDRV_ResetClip(dev);
    return TRUE;
}

/***********************************************************************
 *           PSDRV_DrawArc
 *
 * Does the work of Arc, Chord and Pie. lines is 0, 1 or 2 respectively.
 */
static BOOL PSDRV_DrawArc( PHYSDEV dev, INT left, INT top,
                           INT right, INT bottom, INT xstart, INT ystart,
                           INT xend, INT yend, int lines )
{
    INT x, y, h, w;
    double start_angle, end_angle, ratio;
    RECT rect;
    POINT start, end;

    rect.left = left;
    rect.top = top;
    rect.right = right;
    rect.bottom = bottom;
    LPtoDP( dev->hdc, (POINT *)&rect, 2 );
    start.x = xstart;
    start.y = ystart;
    end.x = xend;
    end.y = yend;
    LPtoDP( dev->hdc, &start, 1 );
    LPtoDP( dev->hdc, &end, 1 );

    x = (rect.left + rect.right) / 2;
    y = (rect.top + rect.bottom) / 2;
    w = rect.right - rect.left;
    h = rect.bottom - rect.top;

    if(w < 0) w = -w;
    if(h < 0) h = -h;
    ratio = ((double)w)/h;

    /* angle is the angle after the rectangle is transformed to a square and is
       measured anticlockwise from the +ve x-axis */

    start_angle = atan2((double)(y - start.y) * ratio, (double)(start.x - x));
    end_angle = atan2((double)(y - end.y) * ratio, (double)(end.x - x));

    start_angle *= 180.0 / PI;
    end_angle *= 180.0 / PI;

    PSDRV_WriteSpool(dev,"%DrawArc\n", 9);
    PSDRV_SetPen(dev);

    PSDRV_SetClip(dev);
    if(lines == 2) /* pie */
        PSDRV_WriteMoveTo(dev, x, y);
    else
        PSDRV_WriteNewPath( dev );

    PSDRV_WriteArc(dev, x, y, w, h, start_angle, end_angle);
    if(lines == 1 || lines == 2) { /* chord or pie */
        PSDRV_WriteClosePath(dev);
	PSDRV_Brush(dev,0);
    }
    PSDRV_DrawLine(dev);
    PSDRV_ResetClip(dev);

    return TRUE;
}


/***********************************************************************
 *           PSDRV_Arc
 */
BOOL CDECL PSDRV_Arc( PHYSDEV dev, INT left, INT top, INT right, INT bottom,
                      INT xstart, INT ystart, INT xend, INT yend )
{
    return PSDRV_DrawArc( dev, left, top, right, bottom, xstart, ystart, xend, yend, 0 );
}

/***********************************************************************
 *           PSDRV_Chord
 */
BOOL CDECL PSDRV_Chord( PHYSDEV dev, INT left, INT top, INT right, INT bottom,
                        INT xstart, INT ystart, INT xend, INT yend )
{
    return PSDRV_DrawArc( dev, left, top, right, bottom, xstart, ystart, xend, yend, 1 );
}


/***********************************************************************
 *           PSDRV_Pie
 */
BOOL CDECL PSDRV_Pie( PHYSDEV dev, INT left, INT top, INT right, INT bottom,
                      INT xstart, INT ystart, INT xend, INT yend )
{
    return PSDRV_DrawArc( dev, left, top, right, bottom, xstart, ystart, xend, yend, 2 );
}


/***********************************************************************
 *           PSDRV_Ellipse
 */
BOOL CDECL PSDRV_Ellipse( PHYSDEV dev, INT left, INT top, INT right, INT bottom)
{
    INT x, y, w, h;
    RECT rect;

    TRACE("%d %d - %d %d\n", left, top, right, bottom);

    rect.left   = left;
    rect.top    = top;
    rect.right  = right;
    rect.bottom = bottom;
    LPtoDP( dev->hdc, (POINT *)&rect, 2 );

    x = (rect.left + rect.right) / 2;
    y = (rect.top + rect.bottom) / 2;
    w = rect.right - rect.left;
    h = rect.bottom - rect.top;

    PSDRV_WriteSpool(dev, "%Ellipse\n", 9);
    PSDRV_SetPen(dev);

    PSDRV_SetClip(dev);
    PSDRV_WriteNewPath(dev);
    PSDRV_WriteArc(dev, x, y, w, h, 0.0, 360.0);
    PSDRV_WriteClosePath(dev);
    PSDRV_Brush(dev,0);
    PSDRV_DrawLine(dev);
    PSDRV_ResetClip(dev);
    return TRUE;
}


/***********************************************************************
 *           PSDRV_PolyPolyline
 */
BOOL CDECL PSDRV_PolyPolyline( PHYSDEV dev, const POINT* pts, const DWORD* counts,
                               DWORD polylines )
{
    DWORD polyline, line, total;
    POINT *dev_pts, *pt;

    TRACE("\n");

    for (polyline = total = 0; polyline < polylines; polyline++) total += counts[polyline];
    if (!(dev_pts = HeapAlloc( GetProcessHeap(), 0, total * sizeof(*dev_pts) ))) return FALSE;
    memcpy( dev_pts, pts, total * sizeof(*dev_pts) );
    LPtoDP( dev->hdc, dev_pts, total );

    pt = dev_pts;

    PSDRV_WriteSpool(dev, "%PolyPolyline\n",14);
    PSDRV_SetPen(dev);
    PSDRV_SetClip(dev);

    for(polyline = 0; polyline < polylines; polyline++) {
        PSDRV_WriteMoveTo(dev, pt->x, pt->y);
	pt++;
        for(line = 1; line < counts[polyline]; line++, pt++)
            PSDRV_WriteLineTo(dev, pt->x, pt->y);
    }
    HeapFree( GetProcessHeap(), 0, dev_pts );

    PSDRV_DrawLine(dev);
    PSDRV_ResetClip(dev);
    return TRUE;
}


/***********************************************************************
 *           PSDRV_Polyline
 */
BOOL CDECL PSDRV_Polyline( PHYSDEV dev, const POINT* pt, INT count )
{
    return PSDRV_PolyPolyline( dev, pt, (LPDWORD) &count, 1 );
}


/***********************************************************************
 *           PSDRV_PolyPolygon
 */
BOOL CDECL PSDRV_PolyPolygon( PHYSDEV dev, const POINT* pts, const INT* counts,
                              UINT polygons )
{
    DWORD polygon, total;
    INT line;
    POINT *dev_pts, *pt;

    TRACE("\n");

    for (polygon = total = 0; polygon < polygons; polygon++) total += counts[polygon];
    if (!(dev_pts = HeapAlloc( GetProcessHeap(), 0, total * sizeof(*dev_pts) ))) return FALSE;
    memcpy( dev_pts, pts, total * sizeof(*dev_pts) );
    LPtoDP( dev->hdc, dev_pts, total );

    pt = dev_pts;

    PSDRV_WriteSpool(dev, "%PolyPolygon\n",13);
    PSDRV_SetPen(dev);
    PSDRV_SetClip(dev);
    PSDRV_WriteNewPath(dev);

    for(polygon = 0; polygon < polygons; polygon++) {
        PSDRV_WriteMoveTo(dev, pt->x, pt->y);
	pt++;
        for(line = 1; line < counts[polygon]; line++, pt++)
            PSDRV_WriteLineTo(dev, pt->x, pt->y);
	PSDRV_WriteClosePath(dev);
    }
    HeapFree( GetProcessHeap(), 0, dev_pts );

    if(GetPolyFillMode( dev->hdc ) == ALTERNATE)
        PSDRV_Brush(dev, 1);
    else /* WINDING */
        PSDRV_Brush(dev, 0);

    PSDRV_DrawLine(dev);
    PSDRV_ResetClip(dev);
    return TRUE;
}


/***********************************************************************
 *           PSDRV_Polygon
 */
BOOL CDECL PSDRV_Polygon( PHYSDEV dev, const POINT* pt, INT count )
{
     return PSDRV_PolyPolygon( dev, pt, &count, 1 );
}


/***********************************************************************
 *           PSDRV_SetPixel
 */
COLORREF CDECL PSDRV_SetPixel( PHYSDEV dev, INT x, INT y, COLORREF color )
{
    PSCOLOR pscolor;
    POINT pt;

    pt.x = x;
    pt.y = y;
    LPtoDP( dev->hdc, &pt, 1 );

    PSDRV_SetClip(dev);
    /* we bracket the setcolor in gsave/grestore so that we don't trash
       the current pen colour */
    PSDRV_WriteGSave(dev);
    PSDRV_WriteRectangle( dev, pt.x, pt.y, 0, 0 );
    PSDRV_CreateColor( dev, &pscolor, color );
    PSDRV_WriteSetColor( dev, &pscolor );
    PSDRV_WriteFill( dev );
    PSDRV_WriteGRestore(dev);
    PSDRV_ResetClip(dev);
    return color;
}

/***********************************************************************
 *           PSDRV_PaintRgn
 */
BOOL CDECL PSDRV_PaintRgn( PHYSDEV dev, HRGN hrgn )
{
    RGNDATA *rgndata = NULL;
    RECT *pRect;
    DWORD size, i;

    TRACE("hdc=%p\n", dev->hdc);

    size = GetRegionData(hrgn, 0, NULL);
    rgndata = HeapAlloc( GetProcessHeap(), 0, size );
    if(!rgndata) {
        ERR("Can't allocate buffer\n");
        return FALSE;
    }
    
    GetRegionData(hrgn, size, rgndata);
    if (rgndata->rdh.nCount == 0)
        goto end;

    LPtoDP(dev->hdc, (POINT*)rgndata->Buffer, rgndata->rdh.nCount * 2);

    PSDRV_SetClip(dev);
    PSDRV_WriteNewPath(dev);
    for(i = 0, pRect = (RECT*)rgndata->Buffer; i < rgndata->rdh.nCount; i++, pRect++)
        PSDRV_WriteRectangle(dev, pRect->left, pRect->top, pRect->right - pRect->left, pRect->bottom - pRect->top);

    PSDRV_Brush(dev, 0);
    PSDRV_ResetClip(dev);

 end:
    HeapFree(GetProcessHeap(), 0, rgndata);
    return TRUE;
}
