#include "utri.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "array.h"
#include "bb.h"
#include "eik3.h"
#include "hybrid.h"
#include "mat.h"
#include "mesh3.h"
#include "slerp.h"

utri_spec_s utri_spec_empty() {
  return (utri_spec_s) {
    .eik = NULL,
    .lhat = (size_t)NO_INDEX,
    .l = {(size_t)NO_INDEX, (size_t)NO_INDEX},
    .state = {UNKNOWN, UNKNOWN},
    .xhat = {NAN, NAN, NAN},
    .x = {
      {NAN, NAN, NAN},
      {NAN, NAN, NAN}
    },
    .jet = {
      {.f = INFINITY, .Df = {NAN, NAN, NAN}},
      {.f = INFINITY, .Df = {NAN, NAN, NAN}}
    },
    .orig_index = (size_t)NO_INDEX
  };
}

utri_spec_s utri_spec_from_eik(eik3_s const *eik, size_t l, size_t l0, size_t l1) {
  state_e const *state = eik3_get_state_ptr(eik);
  return (utri_spec_s) {
    .eik = eik,
    .lhat = l,
    .l = {l0, l1},
    .state = {state[l0], state[l1]},
    .xhat = {NAN, NAN, NAN},
    .x = {
      {NAN, NAN, NAN},
      {NAN, NAN, NAN}
    },
    .jet = {
      {.f = INFINITY, .Df = {NAN, NAN, NAN}},
      {.f = INFINITY, .Df = {NAN, NAN, NAN}}
    }
  };
}

utri_spec_s utri_spec_from_eik_without_l(eik3_s const *eik, dbl const x[3],
                                         size_t l0, size_t l1) {
  state_e const *state = eik3_get_state_ptr(eik);
  return (utri_spec_s) {
    .eik = eik,
    .lhat = (size_t)NO_INDEX,
    .l = {l0, l1},
    .state = {state[l0], state[l1]},
    .xhat = {x[0], x[1], x[2]},
    .x = {
      {NAN, NAN, NAN},
      {NAN, NAN, NAN}
    },
    .jet = {
      {.f = INFINITY, .Df = {NAN, NAN, NAN}},
      {.f = INFINITY, .Df = {NAN, NAN, NAN}}
    }
  };
}

utri_spec_s utri_spec_from_raw_data(dbl3 const x, dbl3 const Xt[2], jet31t const jet[2]) {
  return (utri_spec_s) {
    .eik = NULL,
    .lhat = NO_INDEX,
    .l = {NO_INDEX, NO_INDEX},
    .state = {UNKNOWN, UNKNOWN},
    .xhat = {x[0], x[1], x[2]},
    .x = {
      {Xt[0][0], Xt[0][1], Xt[0][2]},
      {Xt[1][0], Xt[1][1], Xt[1][2]}
    },
    .jet = {jet[0], jet[1]}
  };
}

struct utri {
  dbl lam;
  dbl f;
  dbl Df;
  dbl x_minus_xb[3];
  dbl L;

  size_t l, l0, l1;
  dbl x[3];
  dbl x0[3];
  dbl x1[3];
  dbl x1_minus_x0[3];
  state_e state[2];
  bb31 T;

  size_t num_shadow;

  size_t orig_index; // original index
};

void utri_alloc(utri_s **utri) {
  *utri = malloc(sizeof(utri_s));
}

void utri_dealloc(utri_s **utri) {
  free(*utri);
  *utri = NULL;
}

void utri_set_lambda(utri_s *utri, dbl lam) {
  utri->lam = lam;

  dbl xb[3];
  dbl3_saxpy(lam, utri->x1_minus_x0, utri->x0, xb);
  dbl3_sub(utri->x, xb, utri->x_minus_xb);
  utri->L = dbl3_norm(utri->x_minus_xb);

  dbl dL_dlam = -dbl3_dot(utri->x1_minus_x0, utri->x_minus_xb)/utri->L;

  dbl b[2] = {1 - lam, lam};
  dbl T = bb31_f(&utri->T, b);

  utri->f = T + utri->L;

  dbl a[2] = {-1, 1};
  dbl dT_dlam = bb31_df(&utri->T, b, a);

  utri->Df = dT_dlam + dL_dlam;
}

static dbl utri_hybrid_f(dbl lam, utri_s *utri) {
  utri_set_lambda(utri, lam);
  return utri->Df;
}

bool utri_init(utri_s *u, utri_spec_s const *spec) {
  bool passed_lhat = spec->lhat != (size_t)NO_INDEX;
  bool passed_l0 = spec->l[0] != (size_t)NO_INDEX;
  bool passed_l1 = spec->l[1] != (size_t)NO_INDEX;
  bool passed_l = passed_l0 && passed_l1;

  bool passed_jet0 = (spec->state[0] == VALID || spec->state[0] == UNKNOWN)
    && jet31t_is_finite(&spec->jet[0]);
  bool passed_jet1 = (spec->state[1] == VALID || spec->state[1] == UNKNOWN)
    && jet31t_is_finite(&spec->jet[1]);
  bool passed_jet = passed_jet0 && passed_jet1;

#if JMM_DEBUG
  /* Validate spec before doing anything else */

  bool passed_xhat = dbl3_isfinite(spec->xhat);
  assert(passed_lhat ^ passed_xhat); // exactly one of these

  if (passed_l0 || passed_l1)
    assert(passed_l);

  bool passed_x0 = dbl3_isfinite(spec->x[0]);
  bool passed_x1 = dbl3_isfinite(spec->x[1]);
  bool passed_x = passed_x0 && passed_x1;
  if (passed_x0 || passed_x1)
    assert(passed_x);

  assert(passed_l ^ passed_x); // pass exactly one of these

  bool passed_state0 = spec->state[0] != UNKNOWN;
  bool passed_state1 = spec->state[1] != UNKNOWN;
  bool passed_state = passed_state0 && passed_state1;
  if (passed_state0 || passed_state1)
    assert(passed_state);

  if (passed_jet0 || passed_jet1)
    assert(passed_jet);

  assert(passed_jet ^ passed_l); // exactly one of these
#endif

  /* Initialize `u` */

  u->f = INFINITY;

  u->lam = u->f = u->Df = u->L = NAN;
  u->x_minus_xb[0] = u->x_minus_xb[1] = u->x_minus_xb[2] = NAN;

  mesh3_s const *mesh = spec->eik ? eik3_get_mesh(spec->eik) : NULL;

  u->l = spec->lhat;

  if (passed_lhat) {
    assert(mesh);
    mesh3_copy_vert(mesh, u->l, u->x);
  } else {
    dbl3_copy(spec->xhat, u->x);
  }

  u->l0 = spec->l[0];
  u->l1 = spec->l[1];

  if (passed_l) {
    mesh3_copy_vert(mesh, u->l0, u->x0);
    mesh3_copy_vert(mesh, u->l1, u->x1);
  } else { // passed_x
    dbl3_copy(spec->x[0], u->x0);
    dbl3_copy(spec->x[1], u->x1);
  }

  dbl3_sub(u->x1, u->x0, u->x1_minus_x0);

  memcpy(u->state, spec->state, sizeof(state_e[2]));

  /* Grab the jets for interpolating over the base of the
   * update. These will come from `eik` or `spec`, depending on if we
   * passed them. */
  jet31t jet[2];
  for (size_t i = 0; i < 2; ++i)
    jet[i] = passed_jet ?
      spec->jet[i] :
      jet31t_from_jet32t(eik3_get_jet(spec->eik, spec->l[i]));

  bool pt_src[2];
  for (size_t i = 0; i < 2; ++i)
    pt_src[i] = jet31t_is_point_source(&jet[i]);

  /* If we're solving a point source problem, then we *have* found a
   * point source, and we shouldn't try do a triangle update
   * involving this point. */
  if (spec->eik &&
      eik3_get_ftype(spec->eik) == FTYPE_POINT_SOURCE &&
      (pt_src[0] ^ pt_src[1]))
    return false;

  /* If the jets at the update vertices are finite, we can go ahead
   * and do the interpolation now. On the other hand, if they aren't,
   * we assume that: 1) this is a diffracting update, and 2) we have
   * supplied precomputed boundary data for this update. */
  if (pt_src[0] && pt_src[1]) {
    /* use the precomputed boundary data */
    assert(passed_l);
    assert(mesh3_is_diff_edge(mesh, spec->l));
    assert(eik3_get_bde_bc(spec->eik, spec->l, &u->T));
  } else {
    dbl Xt[2][3];
    dbl3_copy(u->x0, Xt[0]);
    dbl3_copy(u->x1, Xt[1]);
    bb31_init_from_jets(&u->T, jet, Xt);
  }

  u->orig_index = spec->orig_index;

  return true;
}

void utri_solve(utri_s *utri) {
  dbl lam, f[2];

  if (hybrid((hybrid_cost_func_t)utri_hybrid_f, 0, 1, utri, &lam))
    return;

  utri_set_lambda(utri, 0);
  f[0] = utri->f;

  utri_set_lambda(utri, 1);
  f[1] = utri->f;

  assert(f[0] != f[1]);

  if (f[0] < f[1])
    utri_set_lambda(utri, 0);
  else
    utri_set_lambda(utri, 1);
}

static void get_update_inds(utri_s const *utri, size_t l[2]) {
  l[0] = utri->l0;
  l[1] = utri->l1;
}

static void get_bary_coords(utri_s const *utri, dbl b[2]) {
  b[0] = 1 - utri->lam;
  b[1] = utri->lam;
}

par3_s utri_get_par(utri_s const *u) {
  par3_s par = {.l = {[2] = NO_PARENT}, .b = {[2] = NAN}};
  get_update_inds(u, par.l);
  get_bary_coords(u, par.b);
  return par;
}

dbl utri_get_value(utri_s const *u) {
  return u->f;
}

void utri_get_jet31t(utri_s const *utri, jet31t *jet) {
  jet->f = utri->f;
  dbl3_dbl_div(utri->x_minus_xb, utri->L, jet->Df);
}

void utri_get_jet32t(utri_s const *utri, jet32t *jet) {
  utri_get_jet31t(utri, (jet31t *)jet);

  /* After using `utri_get_jet31t` to compute the eikonal and its
   * gradient, we compute the Hessian. This assumes that edge
   * diffraction has occurred, with the edge being defined by the
   * affine hull of [x0, x1] (the base of the triangle update).
   *
   * This should work even if edge difraction has not occurred,
   * although our goal is to avoid doing this as much as is
   * practical. */

  dbl3 v1;
  dbl3_normalized(utri->x1_minus_x0, v1);

  dbl3 x_minus_x0;
  dbl3_sub(utri->x, utri->x0, x_minus_x0);

  dbl s = dbl3_dot(v1, x_minus_x0);
  dbl3 xproj;
  dbl3_saxpy(s, v1, utri->x0, xproj);

  dbl3 v2;
  dbl3_sub(utri->x, xproj, v2);
  dbl x_minus_xproj_norm = dbl3_normalize(v2);

  dbl3 q1;
  dbl3_cross(v1, v2, q1);

  dbl3 q2;
  dbl3_cross(jet->Df, q1, q2);

  dbl33 outer1;
  dbl3_outer(q1, q1, outer1);
  dbl33_dbl_div_inplace(outer1, x_minus_xproj_norm);

  dbl33 outer2;
  dbl3_outer(q2, q2, outer2);
  dbl33_dbl_div_inplace(outer2, jet->f);

  dbl33_add(outer1, outer2, jet->D2f);
}

static dbl get_lag_mult(utri_s const *utri) {
  dbl const atol = 1e-15;
  if (utri->lam < atol) {
    return utri->Df;
  } else if (utri->lam > 1 - atol) {
    return -utri->Df;
  } else {
    return 0;
  }
}

static ray3 get_ray(utri_s const *utri) {
  ray3 ray;
  dbl3_saxpy(utri->lam, utri->x1_minus_x0, utri->x0, ray.org);
  dbl3_normalized(utri->x_minus_xb, ray.dir);
  return ray;
}

static dbl get_L(utri_s const *u) {
  return u->L;
}

bool utri_emits_terminal_ray(utri_s const *utri, eik3_s const *eik) {
  par3_s par = utri_get_par(utri);

  // TODO: anything else we need to check?

  return par3_is_on_BC_boundary(&par, eik);
}

bool utri_update_ray_is_physical(utri_s const *utri, eik3_s const *eik) {
  mesh3_s const *mesh = eik3_get_mesh(eik);

  size_t l[2] = {utri->l0, utri->l1};

  /* Check if we're trying to update from a diffracting edge with
   * precomputed boundary conditions. If this is the case, we're
   * solving an edge diffraction problem and are updating from the
   * original diffracting edge. */
  if (eik3_has_bde_bc(eik, l))
    return true;

  /**
   * First, we check if the ray is spuriously emanating from the
   * boundary. We do this by finding the ray direction, and checking
   * to see if the point just before the start of the ray is inside
   * the mesh.
   */

  // TODO: the following section where we check to see if the stuff
  // below gives "an interior ray" can be wrapped up and reused for
  // both this and the corresponding section in utetra.c...

  // TODO: we can accelerate this a bit by verifying that both l[0]
  // and l[1] are boundary indices...

  ray3 ray = get_ray(utri);

  // Get points just before and just after the start of the ray. We
  // perturb forward and backward by one half of the minimum triangle
  // altitude (taken of the entire mesh). This is to ensure that the
  // perturbations are small enough to stay inside neighboring
  // tetrahedra, but also large enough to take into consideration the
  // characteristic length scale of the mesh.
  dbl xm[3], xp[3], h = mesh3_get_min_edge_length(mesh)/4;
  ray3_get_point(&ray, -h, xm);
  ray3_get_point(&ray, h, xp);

  array_s *cells;
  array_alloc(&cells);
  array_init(cells, sizeof(size_t), ARRAY_DEFAULT_CAPACITY);

  int nvc;
  size_t *vc;

  for (int i = 0; i < 2; ++i) {
    nvc = mesh3_nvc(mesh, l[i]);
    vc = malloc(nvc*sizeof(size_t));
    mesh3_vc(mesh, l[i], vc);

    for (int j = 0; j < nvc; ++j)
      if (!array_contains(cells, &vc[j]))
        array_append(cells, &vc[j]);

    free(vc);
  }

  size_t lc;
  bool xm_in_cell = false, xp_in_cell = false;
  for (size_t i = 0; i < array_size(cells); ++i) {
    array_get(cells, i, &lc);
    xm_in_cell |= mesh3_cell_contains_point(mesh, lc, xm);
    xp_in_cell |= mesh3_cell_contains_point(mesh, lc, xp);
    if (xm_in_cell && xp_in_cell)
      break;
  }

  // Free the space used for the adjacent cells since we're done with
  // it now
  array_deinit(cells);
  array_dealloc(&cells);

  // If we didn't find a containing cell, we can conclude that the ray
  // is unphysical!
  if (!xm_in_cell || !xp_in_cell)
    return false;

  // TODO: need to check for boundary faces here! See implementation
  // of `utetra_update_ray_is_physical`.

  /**
   * Next, check and see if the point just before the end of the ray
   * lies in a cell.
   */

  dbl L = get_L(utri);

  dbl xhatm[3];
  ray3_get_point(&ray, L - h, xhatm);

  /* If `utri->l` is unset, we're probably computing a jet for a cut
   * point, in which case we can fairly safely assume we're OK
   * here. Return. */
  if (utri->l == (size_t)NO_INDEX)
    return true;

  nvc = mesh3_nvc(mesh, utri->l);
  vc = malloc(nvc*sizeof(size_t));
  mesh3_vc(mesh, utri->l, vc);

  bool xhatm_in_cell = false;
  for (int i = 0; i < nvc; ++i) {
    xhatm_in_cell = mesh3_cell_contains_point(mesh, vc[i], xhatm);
    if (xhatm_in_cell)
      break;
  }

  free(vc);

  return xhatm_in_cell;
}

int utri_cmp(utri_s const **h1, utri_s const **h2) {
  utri_s const *u1 = *h1;
  utri_s const *u2 = *h2;

  if (u1 == NULL && u2 == NULL) {
    return 0;
  } else if (u2 == NULL) {
    return -1;
  } else if (u1 == NULL) {
    return 1;
  } else {
    dbl T1 = utri_get_value(u1), T2 = utri_get_value(u2);
    if (T1 < T2) {
      return -1;
    } else if (T1 > T2) {
      return 1;
    } else {
      return 0;
    }
  }
}

bool utri_has_interior_point_solution(utri_s const *utri) {
  dbl const atol = 1e-14;
  return (atol < utri->lam && utri->lam < 1 - atol)
    || fabs(get_lag_mult(utri)) <= atol;
}

bool utri_has_orig_index(utri_s const *utri) {
  return utri->orig_index != (size_t)NO_INDEX;
}

size_t utri_get_orig_index(utri_s const *utri) {
  return utri->orig_index;
}

bool utri_is_finite(utri_s const *u) {
  return isfinite(u->f);
}

size_t utri_get_active_ind(utri_s const *utri) {
  dbl const atol = 1e-14;
  if (utri->lam < atol)
    return utri->l0;
  if (utri->lam > 1 - atol)
    return utri->l1;
  return (size_t)NO_INDEX;
}

size_t utri_get_inactive_ind(utri_s const *utri) {
  dbl const atol = 1e-15;
  if (utri->lam < atol)
    return utri->l1;
  if (utri->lam > 1 - atol)
    return utri->l0;
  return (size_t)NO_INDEX;
}

bool utri_contains_update_ind(utri_s const *u, size_t l) {
  return l == u->l0 || l == u->l1;
}

size_t utri_get_l(utri_s const *utri) {
  return utri->l;
}

bool utri_opt_inc_on_other_utri(utri_s const *u, utri_s const *other_u) {
  dbl const atol = 1e-14;
  dbl xlam[3];
  dbl3_saxpy(u->lam, u->x1_minus_x0, u->x0, xlam);
  dbl dist0 = dbl3_dist(other_u->x0, xlam);
  dbl dist1 = dbl3_dist(other_u->x1, xlam);
  return dist0 < atol || dist1 < atol;
}

void utri_get_update_inds(utri_s const *u, size_t l[2]) {
  l[0] = u->l0;
  l[1] = u->l1;
}

void utri_get_t(utri_s const *u, dbl t[3]) {
  dbl3_normalized(u->x_minus_xb, t);
}

dbl utri_get_L(utri_s const *u) {
  return u->L;
}

dbl utri_get_b(utri_s const *u) {
  return u->lam;
}

bool utri_inc_on_diff_edge(utri_s const *u, mesh3_s const *mesh) {
  return mesh3_is_diff_edge(mesh, (size_t[2]) {u->l0, u->l1});
}

void utri_get_xb(utri_s const *u, dbl xb[3]) {
  dbl3_saxpy(u->lam, u->x1_minus_x0, u->x0, xb);
}

/* Check whether `u`'s `l`, `l0`, and `l1` are collinear (i.e.,
 * whether the `utri` is "degenerate"). */
bool utri_is_degenerate(utri_s const *u) {
  dbl const atol = 1e-14;
  dbl dx0[3], dx1[3];
  dbl3_sub(u->x0, u->x, dx0);
  dbl3_sub(u->x1, u->x, dx1);
  dbl dot = dbl3_dot(dx0, dx1)/(dbl3_norm(dx0)*dbl3_norm(dx1));
  return fabs(1 - fabs(dot)) < atol;
}

bool utri_approx_hess(utri_s const *u, dbl h, dbl33 hess) {
  dbl const atol = 1e-14;

  if (h < atol)
    return false;

  utri_s u_ = *u;

  dbl dx[3], t[3];

  dbl33_zero(hess);

  /* Approximate the Hessian using central differences. For each i,
   * compute (t(x + h*e_i) - t(x - h*e_i))/(2*h) and store in the ith
   * row of hess */
  for (size_t i = 0; i < 3; ++i) {
    dbl3_zero(dx);

    /* Compute t(x + h*e_i) */
    dx[i] = h;
    dbl3_add(u->x, dx, u_.x);
    utri_solve(&u_);
    if (fabs(u->lam - u_.lam) < atol)
      return false;
    utri_get_t(&u_, t);
    dbl3_add_inplace(hess[i], t);

    /* Compute t(x - h*e_i) */
    dx[i] = -h;
    dbl3_add(u->x, dx, u_.x);
    utri_solve(&u_);
    if (fabs(u->lam - u_.lam) < atol)
      return false;
    utri_get_t(&u_, t);
    dbl3_sub_inplace(hess[i], t);

    /* Set H[i, :] = (t(x + h*e_i) - t(x - h*e_i))/(2*h) */
    dbl3_dbl_div_inplace(hess[i], 2*h);
  }

  /* Make sure the Hessian is symmetric */
  dbl33_symmetrize(hess);

  return true;
}

bool utri_inc_on_refl_BCs(utri_s const *u, eik3_s const *eik) {
  assert(eik3_get_ftype(eik) == FTYPE_REFLECTION);
  return eik3_has_BCs(eik, u->l0) && eik3_has_BCs(eik, u->l1);
}

bool utri_accept_refl_BCs_update(utri_s const *u, eik3_s const *eik) {
  size_t l = utri_get_l(u);
  size_t le[2]; utri_get_update_inds(u, le);

  assert(eik3_has_BCs(eik, le[0]));
  assert(eik3_has_BCs(eik, le[1]));

  if (eik3_has_BCs(eik, l))
    return false;

  mesh3_s const *mesh = eik3_get_mesh(eik);

  /* First, get the incident reflecting faces. There should only be
   * one that's adjacent to `le`, since we're assuming for now that
   * reflectors consist only of coplanar facets. */
  size_t lf[3];
  if (!eik3_get_refl_bdf_inc_on_diff_edge(eik, le, lf))
    return true;

  dbl xf[3];
  mesh3_get_face_centroid(mesh, lf, xf);

  dbl nu[3];
  mesh3_get_face_normal(mesh, lf, nu);

  dbl const *x = mesh3_get_vert_ptr(mesh, l);

  if (dbl3_dot(nu, x) - dbl3_dot(nu, xf) < 0)
    return true;

  /* Get the edge's tangent and midpoint and the centroid of the
   * reflecting face. */
  dbl e[3]; mesh3_get_edge_tangent(mesh, le, e);
  dbl xm[3]; mesh3_get_edge_midpoint(mesh, le, xm);

  dbl lam = u->lam;

  jet32t jet0 = eik3_get_jet(eik, le[0]);
  if (fabs(lam) < 1e-14 && !dbl3_isfinite(jet0.Df))
    return true;

  jet32t jet1 = eik3_get_jet(eik, le[1]);
  if (fabs(1 - lam) < 1e-14 && !dbl3_isfinite(jet1.Df))
    return true;

  dbl DT[3];
  if (!dbl3_isfinite(jet0.Df) || !dbl3_isfinite(jet1.Df)) {

  } else {
    slerp2(jet0.Df, jet1.Df, DBL2(1 - lam, lam), DT);
  }

  /* First, get the unit vector that supports the reflection boundary
   * (which we'll denote `t`) */

  dbl t[3];
  dbl3_cross(e, DT, t);
  dbl3_normalize(t);

  dbl t_dot_xm = dbl3_dot(t, xm);
  dbl dot = dbl3_dot(t, xf) - t_dot_xm;

  if (dot == 0)
    return dbl3_dot(nu, x) - dbl3_dot(nu, xm) < 0;

  if (dot > 0)
    dbl3_negate(t);

  return dbl3_dot(t, x) - t_dot_xm;
}

static dbl get_lambda(utri_s const *u) {
  return u->lam;
}

bool utris_yield_same_update(utri_s const *utri1, utri_s const *utri2) {
  dbl const atol = 1e-14;

  if (utri1 == NULL || utri2 == NULL)
    return false;

  dbl lam1 = get_lambda(utri1);
  dbl lam2 = get_lambda(utri2);

  if (fabs(lam1 - lam2) > atol)
    return false;

  jet31t jet1, jet2;
  utri_get_jet31t(utri1, &jet1);
  utri_get_jet31t(utri2, &jet2);

  return jet31t_approx_eq(&jet1, &jet2, atol);
}

#if JMM_TEST
bool utri_is_causal(utri_s const *utri) {
  dbl dx0[3], dx1[3];
  dbl3_sub(utri->x0, utri->x, dx0);
  dbl3_sub(utri->x1, utri->x, dx1);

  dbl cos01 = dbl3_dot(dx0, dx1)/(dbl3_norm(dx0)*dbl3_norm(dx1));

  return cos01 >= 0;
}

dbl utri_get_lambda(utri_s const *utri) {
  return get_lambda(utri);
}
#endif
