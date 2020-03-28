#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "cubic.h"
#include "field.h"
#include "vec.h"

// TODO: we want to make the following changes:
//
// instead of having these contexts, we just pass this data to
// functions below which directly compute "complete 2-jets" (we can
// think of the Newton iteration as being an algorithm that
// iteratively refines a 2-jet of a scalar field)
//
// the benefit: everything below becomes "pure", and there's no
// mutating state

typedef struct {
  // Inputs:
  cubic_s T_cubic, Tx_cubic, Ty_cubic;
  dvec2 xy, xy0, xy1;
  field2_s const *slow;

  // Outputs:
  dbl F4;
  dbl F4_eta;
  dbl F4_th;
} F4_context;

void F4_compute(dbl eta, dbl th, F4_context *context);

#ifdef __cplusplus
}
#endif
