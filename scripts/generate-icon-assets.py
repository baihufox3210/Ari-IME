#!/usr/bin/env python3
"""Rasterize the existing Ari SVG for status bars without SVG loaders.

Maintainer tool: requires librsvg, libcairo and libgobject shared libraries.
Generated PNGs are checked in; neither builds nor installations run this tool.
"""
import ctypes as C
from ctypes.util import find_library
from pathlib import Path

SIZES = (16, 22, 24, 32, 48, 64, 128)


def function(lib, name, result, *args):
    fn = getattr(lib, name)
    fn.restype = result
    fn.argtypes = args
    return fn


def main():
    libraries = []
    for name in ('rsvg-2', 'cairo', 'gobject-2.0'):
        path = find_library(name)
        if not path:
            raise SystemExit('Missing shared library: ' + name)
        libraries.append(C.CDLL(path))
    rsvg, cairo, gobject = libraries
    load = function(rsvg, 'rsvg_handle_new_from_file', C.c_void_p,
                    C.c_char_p, C.c_void_p)
    render = function(rsvg, 'rsvg_handle_render_document', C.c_int,
                      C.c_void_p, C.c_void_p, C.c_void_p, C.c_void_p)
    surface_new = function(cairo, 'cairo_image_surface_create', C.c_void_p,
                           C.c_int, C.c_int, C.c_int)
    surface_status = function(cairo, 'cairo_surface_status', C.c_int, C.c_void_p)
    context_new = function(cairo, 'cairo_create', C.c_void_p, C.c_void_p)
    save = function(cairo, 'cairo_surface_write_to_png', C.c_int,
                    C.c_void_p, C.c_char_p)
    context_free = function(cairo, 'cairo_destroy', None, C.c_void_p)
    surface_free = function(cairo, 'cairo_surface_destroy', None, C.c_void_p)
    unref = function(gobject, 'g_object_unref', None, C.c_void_p)

    class Rectangle(C.Structure):
        _fields_ = [(field, C.c_double) for field in ('x', 'y', 'width', 'height')]

    data = Path(__file__).resolve().parents[1] / 'data'
    handle = load(str(data / 'ari-ime.svg').encode(), None)
    if not handle:
        raise SystemExit('Cannot load Ari SVG')
    try:
        for size in SIZES:
            output = data / 'icons' / f'{size}x{size}' / 'apps' / 'ari-ime-baihu.png'
            output.parent.mkdir(parents=True, exist_ok=True)
            surface = surface_new(0, size, size)  # Cairo ARGB32
            context = context_new(surface)
            try:
                viewport = Rectangle(0, 0, size, size)
                if surface_status(surface) or not render(handle, context, C.byref(viewport), None):
                    raise RuntimeError('Cannot render icon at size ' + str(size))
                if save(surface, str(output).encode()):
                    raise RuntimeError('Cannot write ' + str(output))
            finally:
                context_free(context)
                surface_free(surface)
    finally:
        unref(handle)


if __name__ == '__main__':
    main()
