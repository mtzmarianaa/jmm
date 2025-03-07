#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "array.h"
#include "common.h"
#include "geom.h"
#include "jet.h"
#include "par.h"

typedef struct utetra utetra_s;

void utetra_alloc(utetra_s **cf);
void utetra_dealloc(utetra_s **cf);
void utetra_init(utetra_s *u, eik3_s const *eik, size_t lhat, uint3 const l);
bool utetra_is_degenerate(utetra_s const *u);
void utetra_solve(utetra_s *cf, dbl const *lam);
dbl utetra_get_value(utetra_s const *cf);
void utetra_get_jet31t(utetra_s const *cf, jet31t *jet);
bool utetra_has_interior_point_solution(utetra_s const *cf);
bool utetra_is_backwards(utetra_s const *utetra, eik3_s const *eik);
bool utetra_diff(utetra_s const *utetra, mesh3_s const *mesh, size_t const l[3]);
bool utetra_ray_is_occluded(utetra_s const *utetra, eik3_s const *eik);
bool utetra_updated_from_refl_BCs(utetra_s const *utetra, eik3_s const *eik);
int utetra_get_num_interior_coefs(utetra_s const *utetra);
size_t utetra_get_l(utetra_s const *utetra);
size_t utetra_get_active_inds(utetra_s const *utetra, size_t l[3]);
par3_s utetra_get_parent(utetra_s const *utetra);
dbl utetra_get_L(utetra_s const *u);
void utetra_get_other_inds(utetra_s const *utetra, size_t li, size_t l[2]);
bool utetra_index_is_active(utetra_s const *utetra, size_t l);
bool utetra_has_inds(utetra_s const *utetra, size_t lhat, uint3 const l);
bool utetra_is_bracketed_by_utetras(utetra_s const *utetra, array_s const *utetras);

bool utetras_have_same_minimizer(utetra_s const *u1, utetra_s const *u2);
bool utetras_have_same_inds(utetra_s const *u1, utetra_s const *u2);

#if JMM_TEST
void utetra_step(utetra_s *u);
void utetra_get_lambda(utetra_s const *u, dbl lam[2]);
void utetra_set_lambda(utetra_s *u, dbl const lam[2]);
size_t utetra_get_num_iter(utetra_s const *u);
#endif

#ifdef __cplusplus
}
#endif
