#pragma once

#include "onebit_canvas.hpp"

#include <filesystem>

using cairo_surface_t = onebit::Surface;
using cairo_t = onebit::Context;

enum cairo_status_t
{
    CAIRO_STATUS_SUCCESS = 0,
    CAIRO_STATUS_INVALID_STATUS = 1,
    CAIRO_STATUS_WRITE_ERROR = 2
};

enum cairo_format_t
{
    CAIRO_FORMAT_ARGB32 = 0
};

enum cairo_antialias_t
{
    CAIRO_ANTIALIAS_DEFAULT = 0,
    CAIRO_ANTIALIAS_NONE = 1
};

enum cairo_operator_t
{
    CAIRO_OPERATOR_OVER = 0,
    CAIRO_OPERATOR_CLEAR = 1
};

enum cairo_line_cap_t
{
    CAIRO_LINE_CAP_ROUND = 0
};

enum cairo_line_join_t
{
    CAIRO_LINE_JOIN_ROUND = 0
};


inline cairo_surface_t* cairo_image_surface_create( cairo_format_t, int width, int height )
{
    return onebit::onebit_image_surface_create( width, height );
}


inline cairo_status_t cairo_surface_status( cairo_surface_t* surface )
{
    return surface ? CAIRO_STATUS_SUCCESS : CAIRO_STATUS_INVALID_STATUS;
}


inline void cairo_surface_destroy( cairo_surface_t* surface )
{
    onebit::onebit_surface_destroy( surface );
}


inline cairo_t* cairo_create( cairo_surface_t* surface )
{
    return onebit::onebit_create( surface );
}


inline cairo_status_t cairo_status( cairo_t* context )
{
    return context ? CAIRO_STATUS_SUCCESS : CAIRO_STATUS_INVALID_STATUS;
}


inline void cairo_destroy( cairo_t* context )
{
    onebit::onebit_destroy( context );
}


inline void cairo_set_antialias( cairo_t*, cairo_antialias_t ) {}


inline void cairo_set_source_rgba( cairo_t* context, double r, double g, double b, double a )
{
    onebit::cairo_like::set_source_rgba( context, r, g, b, a );
}


inline void cairo_paint( cairo_t* context )
{
    onebit::onebit_paint( context );
}


inline void cairo_set_line_cap( cairo_t*, cairo_line_cap_t ) {}


inline void cairo_set_line_join( cairo_t*, cairo_line_join_t ) {}


inline void cairo_surface_flush( cairo_surface_t* ) {}


inline cairo_status_t cairo_surface_write_to_bmp( cairo_surface_t* surface, const std::filesystem::path& path,
                                                  int dpiX = 0, int dpiY = 0 )
{
    return onebit::onebit_surface_write_to_bmp( surface, path, dpiX, dpiY )
                   ? CAIRO_STATUS_SUCCESS
                   : CAIRO_STATUS_WRITE_ERROR;
}


inline void cairo_set_line_width( cairo_t*, double ) {}


inline void cairo_set_operator( cairo_t* context, cairo_operator_t op )
{
    onebit::onebit_set_clear_mode( context, op == CAIRO_OPERATOR_CLEAR );
}


inline void cairo_set_dash( cairo_t*, const double*, int, double ) {}


inline void cairo_move_to( cairo_t* context, double x, double y )
{
    onebit::onebit_move_to( context, x, y );
}


inline void cairo_line_to( cairo_t* context, double x, double y )
{
    onebit::onebit_line_to( context, x, y );
}


inline void cairo_close_path( cairo_t* context )
{
    onebit::onebit_close_path( context );
}


inline void cairo_fill( cairo_t* context )
{
    onebit::onebit_fill( context );
}


inline void cairo_stroke( cairo_t* context )
{
    onebit::onebit_clear_path( context );
}


inline void cairo_rectangle( cairo_t* context, double x, double y, double width, double height )
{
    onebit::onebit_rectangle( context, x, y, width, height );
}


inline void cairo_arc( cairo_t*, double, double, double, double, double ) {}


inline void cairo_arc_negative( cairo_t*, double, double, double, double, double ) {}


inline const onebit::RasterStats& cairo_get_raster_stats( cairo_t* context )
{
    static const onebit::RasterStats empty;
    return context ? context->Stats() : empty;
}
