#include "collapse_c.h"
// #include "data.table.h"
// #ifndef USE_RINTERNALS
// #define USE_RINTERNALS
// #endif
#include <math.h>

void matCopyAttr(SEXP out, SEXP x, SEXP Rdrop, int ng) {
  SEXP dn = getAttrib(x, R_DimNamesSymbol);
  SEXP cn = isNull(dn) ? R_NilValue : VECTOR_ELT(dn, 1); // PROTECT ??
  if(ng == 0 && asLogical(Rdrop)) {
    if(length(cn)) setAttrib(out, R_NamesSymbol, cn);
  } else {
    int nprotect = 1;
    SEXP dim = PROTECT(duplicate(getAttrib(x, R_DimSymbol)));
    INTEGER(dim)[0] = ng == 0 ? 1 : ng;
    dimgets(out, dim);
    if(length(cn)) {
      ++nprotect;
      SEXP dn = PROTECT(allocVector(VECSXP, 2));
      SET_VECTOR_ELT(dn, 0, R_NilValue);
      SET_VECTOR_ELT(dn, 1, cn);
      dimnamesgets(out, dn);
    }
    if(OBJECT(x) == 0) copyMostAttrib(x, out);
    UNPROTECT(nprotect);
  }
}

void DFcopyAttr(SEXP out, SEXP x, int ng) {
  SHALLOW_DUPLICATE_ATTRIB(out, x);
  if(OBJECT(x) != 0) { // No attributes for plain lists
    if(ng == 0) {
      setAttrib(out, R_RowNamesSymbol, ScalarInteger(1));
    } else {
      SEXP rn = PROTECT(allocVector(INTSXP, 2)); // Needed here, now unsafe to pass uninitialized vectors to R_RowNamesSymbol.
      INTEGER(rn)[0] = NA_INTEGER;
      INTEGER(rn)[1] = -ng;
      setAttrib(out, R_RowNamesSymbol, rn);
      UNPROTECT(1);
    }
  }
}

// Faster than rep_len(value, n) and slightly faster than matrix(value, n) (which in turn is faster than rep_len)...
SEXP falloc(SEXP value, SEXP n) {
  int l = asInteger(n), tval = TYPEOF(value);
  if(length(value) > 1) error("Must supply a single value to alloc()");
  SEXP out = PROTECT(allocVector(tval, l));
  switch(tval) {
    case INTSXP:
    case LGLSXP: {
      int val = asInteger(value), *pout = INTEGER(out);
      if(val == 0) memset(pout, 0, l*sizeof(int));
      else for(int i = 0; i != l; ++i) pout[i] = val;
      break;
    }
    case REALSXP: {
      double val = asReal(value), *pout = REAL(out);
      if(val == 0.0) memset(pout, 0.0, l*sizeof(double));
      else for(int i = 0; i != l; ++i) pout[i] = val;
      break;
    }
    case STRSXP: {
      SEXP val = asChar(value), *pout = STRING_PTR(out);
      for(int i = 0; i != l; ++i) pout[i] = val;
      break;
    }
    case VECSXP: {
      SEXP *pout = SEXPPTR(out);
      for(int i = 0; i != l; ++i) pout[i] = value;
      break;
    }
    default: error("Not supportd SEXP Type in alloc()");
  }
  copyMostAttrib(value, out);
  UNPROTECT(1);
  return out;
}


SEXP groups2GRP(SEXP x, SEXP lx, SEXP gs) {
  int l = length(x);
  SEXP out = PROTECT(allocVector(INTSXP, asInteger(lx)));
  int *pout = INTEGER(out)-1, *pgs = INTEGER(gs);
  // SEXP *px = VECTOR_PTR(x); // -> Depreciated interface: https://github.com/hadley/r-internals/blob/ea892fa79bbffe961e78dbe9c90ce4ca3bf2d9bc/vectors.md
  // Matt Dowle Commented:
  // VECTOR_PTR does exist but returns 'not safe to return vector pointer' when USE_RINTERNALS is not defined.
  // VECTOR_DATA and LIST_POINTER exist too but call VECTOR_PTR. All are clearly not intended to be used by packages.
  // The concern is overhead inside VECTOR_ELT() biting when called repetitively in a loop like we do here. That's why
  // we take the R API (INTEGER()[i], REAL()[i], etc) outside loops for the simple types even when not parallel. For this
  // type list case (VECSXP) it might be that some items are ALTREP for example, so we really should use the heavier
  // _ELT accessor (VECTOR_ELT) inside the loop in this case.
  SEXP *px = SEXPPTR(x);

  for(int j = l; j--; ) { // This can go in any direction..
    // SEXP column = VECTOR_ELT(x, j);
    int *pcolumn = INTEGER(px[j]), jp = j+1;
    for(int i = pgs[j]; i--; ) pout[pcolumn[i]] = jp; // This can go in any direction...
  }
  UNPROTECT(1);
  return out;
}

// Note: Only supports numeric data!!!!
SEXP lassign(SEXP x, SEXP s, SEXP rows, SEXP fill) {
  int l = length(x), tr = TYPEOF(rows), ss = asInteger(s), rs = LENGTH(rows);
  SEXP out = PROTECT(allocVector(VECSXP, l));
  // SEXP *px = VECTOR_PTR(x); // -> Depreciated interface: https://github.com/hadley/r-internals/blob/ea892fa79bbffe961e78dbe9c90ce4ca3bf2d9bc/vectors.md
  SEXP *px = SEXPPTR(x);
  double dfill = asReal(fill);

  if(tr == INTSXP) {
    int *rowsv = INTEGER(rows); //, vs = ss * sizeof(double);
    for(int j = l; j--; ) {
      SEXP column = px[j]; // VECTOR_ELT(x, j);
      if(length(column) != rs) error("length(rows) must match nrow(x)");
      SEXP outj;
      SET_VECTOR_ELT(out, j, outj = allocVector(REALSXP, ss));
      double *pcolumn = REAL(column), *poutj = REAL(outj);
      // memset(poutj, dfill, vs); // cannot memset missing values... can only memset 0
      for(int i = ss; i--; ) poutj[i] = dfill;
      for(int i = 0; i != rs; ++i) poutj[rowsv[i]-1] = pcolumn[i];
      SHALLOW_DUPLICATE_ATTRIB(outj, column);
    }
  } else if(tr == LGLSXP) {
    int *rowsv = LOGICAL(rows);
    if(ss != rs) error("length(rows) must match length(s) if rows is a logical vector");
    for(int j = l; j--; ) {
      SEXP column = px[j]; // VECTOR_ELT(x, j);
      SEXP outj;
      SET_VECTOR_ELT(out, j, outj = allocVector(REALSXP, ss));
      double *pcolumn = REAL(column), *poutj = REAL(outj);
      for(int i = 0, k = 0; i != rs; ++i) poutj[i] = rowsv[i] ? pcolumn[k++] : dfill;
      SHALLOW_DUPLICATE_ATTRIB(outj, column);
    }
  } else error("rows must be positive integers or a logical vector");
  SHALLOW_DUPLICATE_ATTRIB(out, x);
  UNPROTECT(1);
  return out;
}

// SEXP CasChar(SEXP x) {
//  return coerceVector(x, STRSXP);
// }

/* Inspired by:
 * do_list2env : .Internal(list2env(x, envir))
 */
SEXP multiassign(SEXP lhs, SEXP rhs, SEXP envir) {
  if(TYPEOF(lhs) != STRSXP) error("lhs needs to be character");
  int n = length(lhs);
  if(n == 1) { // lazy_duplicate appears not necessary (copy-on modify is automatically implemented, and <- also does not use it).
    SEXP nam = installChar(STRING_ELT(lhs, 0));
    defineVar(nam, rhs, envir);
    return R_NilValue;
  }
  if(length(rhs) != n) error("length(lhs) must be equal to length(rhs)");
  SEXP *plhs = STRING_PTR(lhs);
  switch(TYPEOF(rhs)) { // installTrChar translates to native encoding, installChar does the same now, but also is available on older systems.
    case REALSXP: {
      double *prhs = REAL(rhs);
      for(int i = 0; i < n; ++i) {
        SEXP nam = installChar(plhs[i]);
        defineVar(nam, ScalarReal(prhs[i]), envir);
      }
      break;
    }
    case INTSXP: {
      int *prhs = INTEGER(rhs);
      for(int i = 0; i < n; ++i) {
        SEXP nam = installChar(plhs[i]);
        defineVar(nam, ScalarInteger(prhs[i]), envir);
      }
      break;
    }
    case STRSXP: {
      SEXP *prhs = STRING_PTR(rhs);
      for(int i = 0; i < n; ++i) {
        SEXP nam = installChar(plhs[i]);
        defineVar(nam, ScalarString(prhs[i]), envir);
      }
      break;
    }
    case LGLSXP: {
      int *prhs = LOGICAL(rhs);
      for(int i = 0; i < n; ++i) {
        SEXP nam = installChar(plhs[i]);
        defineVar(nam, ScalarLogical(prhs[i]), envir);
      }
      break;
    }
    case VECSXP: { // lazy_duplicate appears not necessary (copy-on modify is automatically implemented, and <- also does not use it).
      for(int i = 0; i < n; ++i) {
        SEXP nam = installChar(plhs[i]);
        defineVar(nam, VECTOR_ELT(rhs, i), envir);
      }
      break;
    }
    default: {
      SEXP rhsl = PROTECT(coerceVector(rhs, VECSXP));
      for(int i = 0; i < n; ++i) {
        SEXP nam = installChar(plhs[i]);
        defineVar(nam, VECTOR_ELT(rhsl, i), envir);
      }
      UNPROTECT(1);
    }
  }
  return R_NilValue;
}


SEXP vlabels(SEXP x, SEXP attrn, SEXP usenam) {
  if(!isString(attrn)) error("'attrn' must be of mode character");
  if(length(attrn) != 1) error("exactly one attribute 'attrn' must be given");
  SEXP sym_attrn = PROTECT(installChar(STRING_ELT(attrn, 0)));
  int l = length(x);
  if(TYPEOF(x) != VECSXP) {
    SEXP labx = getAttrib(x, sym_attrn);
    UNPROTECT(1);
    if(labx == R_NilValue) return ScalarString(NA_STRING);
    return labx;
  }
  SEXP res = PROTECT(allocVector(STRSXP, l));
  SEXP *pres = STRING_PTR(res), *px = SEXPPTR(x);
  for(int i = 0; i < l; ++i) {
    SEXP labxi = getAttrib(px[i], sym_attrn);
    pres[i] = labxi == R_NilValue ? NA_STRING : STRING_ELT(labxi, 0);
  }
  if(asLogical(usenam)) {
    SEXP nam = getAttrib(x, R_NamesSymbol);
    if(TYPEOF(nam) != NILSXP) namesgets(res, nam);
  }
  UNPROTECT(2);
  return res;
}

// Note: ind can be NULL...
SEXP setvlabels(SEXP x, SEXP attrn, SEXP value, SEXP ind) { // , SEXP sc
 if(!isString(attrn)) error("'attrn' must be of mode character");
 if(length(attrn) != 1) error("exactly one attribute 'attrn' must be given");
 if(TYPEOF(x) != VECSXP) error("X must be a list");
 int nprotect = 1, l = length(x), tv = TYPEOF(value); // , scl = asLogical(sc);
 SEXP *px = SEXPPTR(x); // , xsc;
 // if(scl) { // Create shallow copy
 //   if(INHERITS(x, char_datatable)) {
 //     xsc = PROTECT(Calloccol(x));
 //   } else {
 //     xsc = PROTECT(shallow_duplicate(x));
 //   }
 //   ++nprotect;
 //   px = SEXPPTR(xsc);
 // }
 SEXP *pv = px;
 if(tv != NILSXP) {
   if(tv == VECSXP || tv == STRSXP) {
    pv = SEXPPTR(value);
   } else {
    SEXP vl = PROTECT(coerceVector(value, VECSXP));
    pv = SEXPPTR(vl); ++nprotect;
   }
 }
 SEXP sym_attrn = PROTECT(installChar(STRING_ELT(attrn, 0)));
 if(length(ind) == 0) {
   if(tv != NILSXP && l != length(value)) error("length(x) must match length(value)");
   if(tv == NILSXP) {
     for(int i = 0; i < l; ++i) setAttrib(px[i], sym_attrn, R_NilValue);
   } else if(tv == STRSXP) {
     for(int i = 0; i < l; ++i) setAttrib(px[i], sym_attrn, ScalarString(pv[i]));
   } else {
     for(int i = 0; i < l; ++i) setAttrib(px[i], sym_attrn, pv[i]);
   }
 } else {
   if(TYPEOF(ind) != INTSXP) error("vlabels<-: ind must be of type integer");
   int li = length(ind), *pind = INTEGER(ind), ii;
   if(tv != NILSXP && li != length(value)) error("length(ind) must match length(value)");
   if(li == 0 || li > l) error("vlabels<-: length(ind) must be > 0 and <= length(x)");
   if(tv == NILSXP) {
     for(int i = 0; i < li; ++i) {
       ii = pind[i]-1;
       if(ii < 0 || ii >= l) error("vlabels<-: ind must be between 1 and length(x)");
       setAttrib(px[ii], sym_attrn, R_NilValue);
     }
   } else if(tv == STRSXP) {
     for(int i = 0; i < li; ++i) {
       ii = pind[i]-1;
       if(ii < 0 || ii >= l) error("vlabels<-: ind must be between 1 and length(x)");
       setAttrib(px[ii], sym_attrn, ScalarString(pv[i]));
     }
   } else {
     for(int i = 0; i < li; ++i) {
       ii = pind[i]-1;
       if(ii < 0 || ii >= l) error("vlabels<-: ind must be between 1 and length(x)");
       setAttrib(px[ii], sym_attrn, pv[i]);
     }
   }
 }
 UNPROTECT(nprotect);
 // return scl ? xsc : x;
 return x;
}


SEXP setnames(SEXP x, SEXP nam) {
  setAttrib(x, R_NamesSymbol, nam);
  return x;
}

SEXP Cissorted(SEXP x, SEXP strictly) {
   return ScalarLogical(FALSE == isUnsorted(x, (Rboolean)asLogical(strictly)));
}

SEXP fcrosscolon(SEXP x, SEXP ngp, SEXP y, SEXP ckna) {
  int l = length(x), narm = asLogical(ckna);
  if(l != length(y)) error("length mismatch");
  if(TYPEOF(x) != INTSXP) error("x needs to be integer");
  if(TYPEOF(y) != INTSXP) error("y needs to be integer");
  int ng = asInteger(ngp), *px = INTEGER(x), *py = INTEGER(y);
  if(ng > INT_MAX / 2) error("Table larger than INT_MAX/2");

  if(narm) {
    for(int i = 0; i != l; ++i) {
      if(px[i] != NA_INTEGER) {
        if(py[i] == NA_INTEGER) px[i] = NA_INTEGER;
        else px[i] += (py[i] - 1) * ng;
      }
    }
  } else {
    for(int i = 0; i != l; ++i) px[i] += (py[i] - 1) * ng;
  }

  return R_NilValue;
}

SEXP fwtabulate(SEXP x, SEXP w, SEXP ngp, SEXP ckna) {
  int l = length(x), narm = asLogical(ckna), ng = asInteger(ngp), nwl = isNull(w);
  if(TYPEOF(x) != INTSXP) error("x needs to be integer");
  // if(ng > INT_MAX/2) error("Table larger than INT_MAX/2");

  SEXP tab = PROTECT(allocVector(nwl ? INTSXP : REALSXP, ng));
  int *px = INTEGER(x);

  if(nwl) {
    int *ptab = INTEGER(tab);
    memset(ptab, 0, sizeof(int) * ng); --ptab;
    if(narm) {
      for(int i = 0; i != l; ++i) if(px[i] != NA_INTEGER) ++ptab[px[i]];
    } else {
      for(int i = 0; i != l; ++i) ++ptab[px[i]];
    }
  } else {
    if(length(w) != l) error("length(w) must be equal to length(x)");
    double *ptab = REAL(tab);
    memset(ptab, 0.0, sizeof(double) * ng); --ptab;
    switch(TYPEOF(w)) {
      case REALSXP: {
        double *pw = REAL(w);
        if(narm) {
          for(int i = 0; i != l; ++i) if(px[i] != NA_INTEGER && NISNAN(pw[i])) ptab[px[i]] += pw[i];
        } else {
          for(int i = 0; i != l; ++i) if(NISNAN(pw[i])) ptab[px[i]] += pw[i];
        }
        break;
      }
      case INTSXP:
      case LGLSXP: {
        int *pw = INTEGER(w);
        if(narm) {
          for(int i = 0; i != l; ++i) if(px[i] != NA_INTEGER && pw[i] != NA_INTEGER) ptab[px[i]] += pw[i];
        } else {
          for(int i = 0; i != l; ++i) if(pw[i] != NA_INTEGER) ptab[px[i]] += pw[i];
        }
        break;
      }
      default: error("Unsupported weights type!");
    }
  }

  UNPROTECT(1);
  return tab;
}

// Recursive function: doesn't work in C99 Standard
// int fgcd(int a, int b) {
//    if(b == 0) return a;
//    else return fcgd(b, a % b);
// }

// https://www.datamentor.io/r-programming/examples/gcd-hcf/
// https://stackoverflow.com/questions/7500128/how-to-use-operator-for-float-values-in-c
// https://www.tutorialspoint.com/find-out-the-gcd-of-two-numbers-using-while-loop-in-c-language
double dgcd(double a, double b) {
  double rem;
  while(b > 0.000001) // check for b>0 condition because in a % b, b should not equal to zero
  {
    rem = fmod(a, b);
    a = b;
    b = rem;
  }
  return a;
}

int igcd(int a, int b) {
  int rem;
  while(b != 0) // check for b!=0 condition because in a % b, b should not equal to zero
  {
    rem = a % b;
    a = b;
    b = rem;
  }
  return a;
}

// Greatest common divisor of a vector of numeric values
// Note that the function expects positive values only (use abs() in R beforehand)
// Also best to sort values before entering this function. For example c(0.25, 0) gives 0.25, not 0
SEXP vecgcd(SEXP x) {

  int n = length(x);
  if(n == 1) return x;

  switch(TYPEOF(x)) {
    case INTSXP:
    case LGLSXP:
    {
      int *px = INTEGER(x), gcd = px[0];
      for(int i = 1; i < n; ++i) {
        if(gcd <= 1) break;
        gcd = igcd(px[i], gcd);
      }
      if(gcd == 0) return ScalarInteger(1);
      return ScalarInteger(gcd);
      // fixest solution: https://github.com/lrberge/fixest/blob/master/src/misc_funs.cpp
      // int *px = INTEGER(x), gcd = px[0], ok = 0;
      // for(int i = 1; i < n; ++i) if(gcd > px[i]) gcd = px[i];
      // while(ok == 0 && gcd > 1) {
      //   ok = 1;
      //   for(int i = 0; i < n; ++i) {
      //     if(px[i] % gcd != 0) {
      //       gcd--;
      //       ok = 0;
      //       break;
      //     }
      //   }
      // }
    }
    case REALSXP:
    {
      // TODO: Check if double is integer?
      double *px = REAL(x), gcd = px[0];
      for(int i = 1; i < n; ++i) {
        if(gcd < 0.000001) break;
        gcd = dgcd(px[i], gcd);
      }
      if(gcd < 0.000001) error("GCD is approximately zero");
      return ScalarReal(round(gcd * 1000000) / 1000000);
    }
    default: error("Greatest Common Divisor can only be calculated with integer or numeric data");
  }
  return R_NilValue;
}
