/**
 * \file Geodesic.cpp
 * \brief Implementation for GeographicLib::Geodesic class
 *
 * Copyright (c) Charles Karney (2009, 2010) <charles@karney.com>
 * and licensed under the LGPL.  For more information, see
 * http://geographiclib.sourceforge.net/
 *
 * This is a reformulation of the geodesic problem.  The notation is as
 * follows:
 * - at a general point (no suffix or 1 or 2 as suffix)
 *   - phi = latitude
 *   - beta = latitude on auxiliary sphere
 *   - omega = longitude on auxiliary sphere
 *   - lambda = longitude
 *   - alpha = azimuth of great circle
 *   - sigma = arc length along greate circle
 *   - s = distance
 *   - tau = scaled distance (= sigma at multiples of pi/2)
 * - at northwards equator crossing
 *   - beta = phi = 0
 *   - omega = lambda = 0
 *   - alpha = alpha0
 *   - sigma = s = 0
 * - a 12 suffix means a difference, e.g., s12 = s2 - s1.
 * - s and c prefixes mean sin and cos
 **********************************************************************/

#include "GeographicLib/GeodesicLine.hpp"

#define GEOGRAPHICLIB_GEODESICLINE_CPP "$Id$"

RCSID_DECL(GEOGRAPHICLIB_GEODESICLINE_CPP)
RCSID_DECL(GEOGRAPHICLIB_GEODESICLINE_HPP)

namespace GeographicLib {

  using namespace std;

  GeodesicLine::GeodesicLine(const Geodesic& g,
                             real lat1, real lon1, real azi1,
                             unsigned caps) throw()
    : _a(g._a)
    , _r(g._r)
    , _b(g._b)
    , _c2(g._c2)
    , _f1(g._f1)
      // Always allow latitude and azimuth
    , _caps(caps | LATITUDE | AZIMUTH)
  {
    azi1 = Geodesic::AngNormalize(azi1);
    // Guard against underflow in salp0
    azi1 = Geodesic::AngRound(azi1);
    lon1 = Geodesic::AngNormalize(lon1);
    _lat1 = lat1;
    _lon1 = lon1;
    _azi1 = azi1;
    // alp1 is in [0, pi]
    real alp1 = azi1 * Constants::degree();
    // Enforce sin(pi) == 0 and cos(pi/2) == 0.  Better to face the ensuing
    // problems directly than to skirt them.
    _salp1 =     azi1  == -180 ? 0 : sin(alp1);
    _calp1 = abs(azi1) ==   90 ? 0 : cos(alp1);
    real cbet1, sbet1, phi;
    phi = lat1 * Constants::degree();
    // Ensure cbet1 = +epsilon at poles
    sbet1 = _f1 * sin(phi);
    cbet1 = abs(lat1) == 90 ? Geodesic::eps2 : cos(phi);
    Geodesic::SinCosNorm(sbet1, cbet1);

    // Evaluate alp0 from sin(alp1) * cos(bet1) = sin(alp0),
    _salp0 = _salp1 * cbet1; // alp0 in [0, pi/2 - |bet1|]
    // Alt: calp0 = hypot(sbet1, calp1 * cbet1).  The following
    // is slightly better (consider the case salp1 = 0).
    _calp0 = Math::hypot(_calp1, _salp1 * sbet1);
    // Evaluate sig with tan(bet1) = tan(sig1) * cos(alp1).
    // sig = 0 is nearest northward crossing of equator.
    // With bet1 = 0, alp1 = pi/2, we have sig1 = 0 (equatorial line).
    // With bet1 =  pi/2, alp1 = -pi, sig1 =  pi/2
    // With bet1 = -pi/2, alp1 =  0 , sig1 = -pi/2
    // Evaluate omg1 with tan(omg1) = sin(alp0) * tan(sig1).
    // With alp0 in (0, pi/2], quadrants for sig and omg coincide.
    // No atan2(0,0) ambiguity at poles since cbet1 = +epsilon.
    // With alp0 = 0, omg1 = 0 for alp1 = 0, omg1 = pi for alp1 = pi.
    _ssig1 = sbet1; _somg1 = _salp0 * sbet1;
    _csig1 = _comg1 = sbet1 != 0 || _calp1 != 0 ? cbet1 * _calp1 : 1;
    Geodesic::SinCosNorm(_ssig1, _csig1); // sig1 in (-pi, pi]
    Geodesic::SinCosNorm(_somg1, _comg1);

    _k2 = sq(_calp0) * g._ep2;
    real eps = _k2 / (2 * (1 + sqrt(1 + _k2)) + _k2);

    if (_caps & CAP_C1) {
      _A1m1 =  Geodesic::A1m1f(eps);
      Geodesic::C1f(eps, _C1a);
      _B11 = Geodesic::SinCosSeries(true, _ssig1, _csig1, _C1a, nC1);
      real s = sin(_B11), c = cos(_B11);
      // tau1 = sig1 + B11
      _stau1 = _ssig1 * c + _csig1 * s;
      _ctau1 = _csig1 * c - _ssig1 * s;
      // Not necessary because C1pa reverts C1a
      //    _B11 = -SinCosSeries(true, _stau1, _ctau1, _C1pa, nC1p);
    }

    if (_caps & CAP_C1p)
      Geodesic::C1pf(eps, _C1pa);

    if (_caps & CAP_C2) {
      _A2m1 =  Geodesic::A2m1f(eps);
      Geodesic::C2f(eps, _C2a);
      _B21 = Geodesic::SinCosSeries(true, _ssig1, _csig1, _C2a, nC2);
    }

    if (_caps & CAP_C3) {
      g.C3f(eps, _C3a);
      _A3c = -g._f * _salp0 * g.A3f(eps);
      _B31 = Geodesic::SinCosSeries(true, _ssig1, _csig1, _C3a, nC3-1);
    }

    if (_caps & CAP_C4) {
      g.C4f(_k2, _C4a);
      // Multiplier = a^2 * e^2 * cos(alpha0) * sin(alpha0)
      _A4 = sq(g._a) * _calp0 * _salp0 * g._e2;
      _B41 = Geodesic::SinCosSeries(false, _ssig1, _csig1, _C4a, nC4);
    }
  }

  Math::real GeodesicLine::Position(bool arcmode, real a12,
                                       unsigned outmask,
                                       real& lat2, real& lon2, real& azi2,
                                       real& s12, real& m12,
                                       real& M12, real& M21,
                                       real& S12)
  const throw() {
    outmask &= _caps & OUT_ALL;
    if (!( Init() && (arcmode || (_caps & DISTANCE_IN & OUT_ALL)) ))
      // Uninitialized or impossible distance calculation requested
      return Math::NaN();

    // Avoid warning about uninitialized B12.
    real sig12, ssig12, csig12, B12 = 0, AB1 = 0;
    if (arcmode) {
      // Interpret a12 as spherical arc length
      sig12 = a12 * Constants::degree();
      real s12a = abs(a12);
      s12a -= 180 * floor(s12a / 180);
      ssig12 = s12a ==  0 ? 0 : sin(sig12);
      csig12 = s12a == 90 ? 0 : cos(sig12);
    } else {
      // Interpret a12 as distance
      real
        tau12 = a12 / (_b * (1 + _A1m1)),
        s = sin(tau12),
        c = cos(tau12);
      // tau2 = tau1 + tau12
      B12 = - Geodesic::SinCosSeries(true, _stau1 * c + _ctau1 * s,
                                     _ctau1 * c - _stau1 * s,
                                     _C1pa, nC1p);
      sig12 = tau12 - (B12 - _B11);
      ssig12 = sin(sig12);
      csig12 = cos(sig12);
    }

    real omg12, lam12, lon12;
    real ssig2, csig2, sbet2, cbet2, somg2, comg2, salp2, calp2;
    // sig2 = sig1 + sig12
    ssig2 = _ssig1 * csig12 + _csig1 * ssig12;
    csig2 = _csig1 * csig12 - _ssig1 * ssig12;
    if (outmask & (DISTANCE | REDUCEDLENGTH | GEODESICSCALE)) {
      if (arcmode)
        B12 = Geodesic::SinCosSeries(true, ssig2, csig2, _C1a, nC1);
      AB1 = (1 + _A1m1) * (B12 - _B11);
    }
    // sin(bet2) = cos(alp0) * sin(sig2)
    sbet2 = _calp0 * ssig2;
    // Alt: cbet2 = hypot(csig2, salp0 * ssig2);
    cbet2 = Math::hypot(_salp0, _calp0 * csig2);
    if (cbet2 == 0)
      // I.e., salp0 = 0, csig2 = 0.  Break the degeneracy in this case
      cbet2 = csig2 = Geodesic::eps2;
    // tan(omg2) = sin(alp0) * tan(sig2)
    somg2 = _salp0 * ssig2; comg2 = csig2;  // No need to normalize
    // tan(alp0) = cos(sig2)*tan(alp2)
    salp2 = _salp0; calp2 = _calp0 * csig2; // No need to normalize
    // omg12 = omg2 - omg1
    omg12 = atan2(somg2 * _comg1 - comg2 * _somg1,
                  comg2 * _comg1 + somg2 * _somg1);

    if (outmask & DISTANCE)
      s12 = arcmode ? _b * ((1 + _A1m1) * sig12 + AB1) : a12;

    if (outmask & LONGITUDE) {
      lam12 = omg12 + _A3c *
        ( sig12 + (Geodesic::SinCosSeries(true, ssig2, csig2, _C3a, nC3-1)
                   - _B31));
      lon12 = lam12 / Constants::degree();
      // Can't use AngNormalize because longitude might have wrapped multiple
      // times.
      lon12 = lon12 - 360 * floor(lon12/360 + real(0.5));
      lon2 = Geodesic::AngNormalize(_lon1 + lon12);
    }

    if (outmask & LATITUDE)
      lat2 = atan2(sbet2, _f1 * cbet2) / Constants::degree();

    if (outmask & AZIMUTH)
      // minus signs give range [-180, 180). 0- converts -0 to +0.
      azi2 = 0 - atan2(-salp2, calp2) / Constants::degree();

    if (outmask & (REDUCEDLENGTH | GEODESICSCALE)) {
      real
        ssig1sq = sq(_ssig1),
        ssig2sq = sq( ssig2),
        w1 = sqrt(1 + _k2 * ssig1sq),
        w2 = sqrt(1 + _k2 * ssig2sq),
        B22 = Geodesic::SinCosSeries(true, ssig2, csig2, _C2a, nC2),
        AB2 = (1 + _A2m1) * (B22 - _B21),
        J12 = (_A1m1 - _A2m1) * sig12 + (AB1 - AB2);
      if (outmask & REDUCEDLENGTH)
        // Add parens around (_csig1 * ssig2) and (_ssig1 * csig2) to ensure
        // accurate cancellation in the case of coincident points.
        m12 = _b * ((w2 * (_csig1 * ssig2) - w1 * (_ssig1 * csig2))
                  - _csig1 * csig2 * J12);
      if (outmask & GEODESICSCALE) {
        M12 = csig12 + (_k2 * (ssig2sq - ssig1sq) *  ssig2 / (w1 + w2)
                        - csig2 * J12) * _ssig1 / w1;
        M21 = csig12 - (_k2 * (ssig2sq - ssig1sq) * _ssig1 / (w1 + w2)
                        - _csig1 * J12) * ssig2 / w2;
      }
    }

    if (outmask & AREA) {
      real
        B42 = Geodesic::SinCosSeries(false, ssig2, csig2, _C4a, nC4),
      // alp12 = alp2 - alp1, used in atan2 so no need to normalized
        salp12 = salp2 * _calp1 - calp2 * _salp1,
        calp12 = calp2 * _calp1 + salp2 * _salp1;
      // The right thing appears to happen if alp1 = +/-180 and alp2 = 0, viz
      // salp12 = -0 and alp12 = -180.  However this depends on the sign being
      // attached to 0 correctly.  The following ensures the correct behavior.
      if (salp12 == 0 && calp12 < 0) {
        salp12 = Geodesic::eps2 * _calp1;
        calp12 = -1;
      }
      S12 = _c2 * atan2(salp12, calp12) + _A4 * (B42 - _B41);
    }

    return arcmode ? a12 : sig12 /  Constants::degree();
  }

  Math::real GeodesicLine::Position(real s12, real& lat2, real& lon2)
    const throw() {
    real t;
    return Position(false, s12,
                    LATITUDE | LONGITUDE,
                    lat2, lon2, t, t, t, t, t, t);
  }

  Math::real GeodesicLine::Position(real s12, real& lat2, real& lon2,
                                    real& azi2) const throw() {
    real t;
    return Position(false, s12,
                    LATITUDE | LONGITUDE | AZIMUTH,
                    lat2, lon2, azi2, t, t, t, t, t);
  }

  Math::real GeodesicLine::Position(real s12, real& lat2, real& lon2,
                                    real& azi2, real& m12) const throw() {
    real t;
    return Position(false, s12,
                    LATITUDE | LONGITUDE |
                    AZIMUTH | REDUCEDLENGTH,
                    lat2, lon2, azi2, t, m12, t, t, t);
  }

  Math::real GeodesicLine::Position(real s12, real& lat2, real& lon2,
                                    real& azi2, real& M12, real& M21)
    const throw() {
    real t;
    return Position(false, s12,
                    LATITUDE | LONGITUDE |
                    AZIMUTH | GEODESICSCALE,
                    lat2, lon2, azi2, t, t, M12, M21, t);
  }

  Math::real GeodesicLine::Position(real s12,
                                    real& lat2, real& lon2, real& azi2,
                                    real& m12, real& M12, real& M21)
    const throw() {
    real t;
    return Position(false, s12,
                    LATITUDE | LONGITUDE | AZIMUTH |
                    REDUCEDLENGTH | GEODESICSCALE,
                    lat2, lon2, azi2, t, m12, M12, M21, t);
  }

  Math::real GeodesicLine::Position(real s12,
                                    real& lat2, real& lon2, real& azi2,
                                    real& m12, real& M12, real& M21,
                                    real& S12) const throw() {
    real t;
    return Position(false, s12,
                    LATITUDE | LONGITUDE | AZIMUTH |
                    REDUCEDLENGTH | GEODESICSCALE | AREA,
                    lat2, lon2, azi2, t, m12, M12, M21, S12);
  }

  void GeodesicLine::ArcPosition(real a12, real& lat2, real& lon2)
    const throw() {
    real t;
    Position(true, a12,
             LATITUDE | LONGITUDE,
             lat2, lon2, t, t, t, t, t, t);
  }

  void GeodesicLine::ArcPosition(real a12,
                                 real& lat2, real& lon2, real& azi2)
    const throw() {
    real t;
    Position(true, a12,
             LATITUDE | LONGITUDE | AZIMUTH,
             lat2, lon2, azi2, t, t, t, t, t);
  }

  void GeodesicLine::ArcPosition(real a12, real& lat2, real& lon2, real& azi2,
                                 real& s12) const throw() {
    real t;
    Position(true, a12,
             LATITUDE | LONGITUDE | AZIMUTH | DISTANCE,
             lat2, lon2, azi2, s12, t, t, t, t);
  }

  void GeodesicLine::ArcPosition(real a12, real& lat2, real& lon2, real& azi2,
                                 real& s12, real& m12) const throw() {
    real t;
    Position(true, a12,
             LATITUDE | LONGITUDE | AZIMUTH |
             DISTANCE | REDUCEDLENGTH,
             lat2, lon2, azi2, s12, m12, t, t, t);
  }

  void GeodesicLine::ArcPosition(real a12, real& lat2, real& lon2, real& azi2,
                                 real& s12, real& M12, real& M21)
    const throw() {
    real t;
    Position(true, a12,
             LATITUDE | LONGITUDE | AZIMUTH |
             DISTANCE | GEODESICSCALE,
             lat2, lon2, azi2, s12, t, M12, M21, t);
  }

  void GeodesicLine::ArcPosition(real a12, real& lat2, real& lon2, real& azi2,
                                 real& s12, real& m12, real& M12, real& M21)
    const throw() {
    real t;
    Position(true, a12,
             LATITUDE | LONGITUDE | AZIMUTH |
             DISTANCE | REDUCEDLENGTH | GEODESICSCALE,
             lat2, lon2, azi2, s12, m12, M12, M21, t);
  }

  void GeodesicLine::ArcPosition(real a12, real& lat2, real& lon2, real& azi2,
                                 real& s12, real& m12, real& M12, real& M21,
                                 real& S12) const throw() {
    Position(true, a12,
             LATITUDE | LONGITUDE | AZIMUTH | DISTANCE |
             REDUCEDLENGTH | GEODESICSCALE | AREA,
             lat2, lon2, azi2, s12, m12, M12, M21, S12);
  }
} // namespace GeographicLib
