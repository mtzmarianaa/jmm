from jmm.defs cimport bool, dbl
from jmm.jet cimport jet3

cdef extern from "bb.h":
    ctypedef struct bb31:
        dbl c[4]

    void bb31_init_from_1d_data(bb31 *bb, const dbl f[2], const dbl Df[2],
                                const dbl x[2])

    ctypedef struct bb33:
        dbl c[20]

    void bb33_init_from_3d_data(bb33 *bb, const dbl f[4], const dbl Df[4][3], const dbl x[4][3])
    void bb33_init_from_jets(bb33 *bb, const jet3 jets[4], const dbl x[4][3])
    dbl bb33_f(const bb33 *bb, const dbl b[4])
    dbl bb33_df(const bb33 *bb, const dbl b[4], const dbl a[4])
    dbl bb33_d2f(const bb33 *bb, const dbl b[4], const dbl a[2][4])
    bool bb33_convex_hull_brackets_value(const bb33 *bb, dbl value)

cdef class Bb31:
    cdef bb31 bb

    @staticmethod
    cdef from_bb31(bb31 bb)

cdef class Bb33:
    cdef bb33 _bb

    @staticmethod
    cdef from_bb33(bb33 bb)
