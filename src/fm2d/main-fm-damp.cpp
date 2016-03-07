
extern "C" {
#include <rsf.h>
}

#include <boost/timer/timer.hpp>
#include <time.h>

#ifdef _OPENMP
#include <omp.h>
#endif

#include "logger.h"
#include "fm-params.h"
#include "sum.h"
#include "ricker-wavelet.h"
#include "velocity.h"
#include "sf-velocity-reader.h"
#include "common.h"
#include "shot-position.h"

#include "spongeabc4d.h"
#include "damp4t10d.h"
#include "sfutil.h"

int main(int argc, char* argv[])
{
  boost::timer::auto_cpu_timer t;

  /* initialize Madagascar */
  sf_init(argc,argv);

  FmParams &params = FmParams::instance();
  Logger::instance().init("fm");

  int nz = params.nz;
  int nx = params.nx;
  int nb = params.nb;
  int ng = params.ng;
  int nt = params.nt;
  int ns = params.ns;
  float dt = params.dt;
  float fm = params.fm;

  SfVelocityReader velReader(params.vinit);
  Velocity v0 = SfVelocityReader::read(params.vinit, nx, nz);

  Damp4t10d fmMethod(dt, params.dx, nb);

  Velocity exvel = fmMethod.expandDomain(v0);

  fmMethod.bindVelocity(exvel);

  std::vector<float> wlt(nt);
  rickerWavelet(&wlt[0], nt, fm, dt, params.amp);

  ShotPosition allSrcPos(params.szbeg, params.sxbeg, params.jsz, params.jsx, ns, nz);
  ShotPosition allGeoPos(params.gzbeg, params.gxbeg, params.jgz, params.jgx, ng, nz);

  for(int is=0; is<ns; is++) {
    boost::timer::cpu_timer timer;

    std::vector<float> p0(exvel.nz * exvel.nx, 0);
    std::vector<float> p1(exvel.nz * exvel.nx, 0);
    std::vector<float> dobs(params.nt * params.ng, 0);
    std::vector<float> trans(params.nt * params.ng, 0);
    ShotPosition curSrcPos = allSrcPos.clipRange(is, is);

    for(int it=0; it<nt; it++) {

      fmMethod.addSource(&p1[0], &wlt[it], curSrcPos);

      TRACE() << format("it %d, sum p after adding source %.20f") % it % sum(p1);

      fmMethod.stepForward(&p0[0], &p1[0]);
      TRACE() << format("it %d, sum p after fd %.20f") % it % sum(p0);


      fmMethod.recordSeis(&dobs[it*ng], &p0[0], allGeoPos);
      TRACE() << format("it %d, sum dobs %.20f") % it % sum(&dobs[it * ng], ng);

      std::swap(p1, p0);

    }

    matrix_transpose(&dobs[0], &trans[0], ng, nt);
    DEBUG() << format("shot %d, time %s") % is % timer.format(2).c_str();

    sf_floatwrite(&trans[0], ng*nt, params.shots);
  }

  return 0;
}
