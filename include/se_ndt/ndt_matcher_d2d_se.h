/*
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2010, AASS Research Center, Orebro University.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of AASS Research Center nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 */
#ifndef NDT_MATCHER_D2D_SE_HH
#define NDT_MATCHER_D2D_SE_HH

#include "ndt_map/ndt_map.h"
#include "ndt_registration/ndt_matcher_d2d.h"
#include "pcl/point_cloud.h"
#include <profiler.hpp>

namespace perception_oru
{

/**
 * This class implements NDT registration for 3D point cloud scans.
 */
class NDTMatcherD2D_SE: public NDTMatcherD2D
{
public:
	Eigen::Matrix<double,6,6> HessianF;
	Eigen::Matrix<double,6,1> score_gradientF;
    double score_best;
	int NumInputs;
    /**
     * Registers multiple point clouds to multiple NDT structures.
     * \param  fixed
     *   Reference data.
     * \param  moving
     *   The output transformation registers this point cloud to \c fixed.
     * \param  T
     *   This is an input/output parameter. The initial value of \c T
     *   gives the initial pose estimate of \c moving. When the
     *   algorithm terminates, \c T holds the registration result.
     */
    bool match( NDTMap **target,
                NDTMap **source,
                Eigen::Transform<double,3,Eigen::Affine,Eigen::ColMajor>& T,
                bool useInitialGuess = false
				);

	bool covariance( NDTMap *** targetNDTMany, NDTMap *** sourceNDTMany,
			Eigen::Transform<double,3,Eigen::Affine,Eigen::ColMajor>& T,
		std::vector<float>& resolutions, Eigen::MatrixXd &cov, bool inverse);

    //compute the score gradient & hessian of multiple point clouds + transformation to NDTs
    // input: moving, fixed, tr, computeHessian
    //output: score_gradient, Hessian, returns: score!
    virtual double derivativesNDT(
        const std::vector<NDTCell*> *source,
        const NDTMap * const * target,
        Eigen::MatrixXd &score_gradient,
        Eigen::MatrixXd &Hessian,
        bool computeHessian,
        bool init_hessian_gradient=true
    );
    //perform line search to find the best descent rate (Mohre&Thuente)
    //adapted from NOX
    double lineSearchMT(
        Eigen::Matrix<double,6,1> &increment,
        std::vector<NDTCell*>  *source,
        NDTMap **target) ;
};

} // end namespace

//#include <ndt_registration/impl/ndt_matcher_d2d.hpp>
#endif
