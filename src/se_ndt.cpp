#include <Eigen/QR>
#include <Eigen/StdVector>
#include <se_ndt/se_ndt.hpp>
#include <numeric>
#include <other.hpp>
#include <thread>
#include <profiler.hpp>
#include <vector>
#include <csignal>
#define COND_SCORE -10000
#define HIST_BINS1 9
#define HIST_BINS2 9
#define RANGE1 25 
#define RANGE2 50 
#define  HIST_ACCURATE_DISTANCE 10//Distance where the histogram is expected to have the same value
#define HIST_FREQ_METER 5 //Recomended 10
#define SIMILARITY_THRESHOLD 0.03

using namespace std;


NDTMatch_SE::NDTMatch_SE(initializer_list<float> b,initializer_list<int> c,initializer_list<float> d,int nIn,int max_iter):resolutions(b),resolutions_order(c),size(d),NumInputs(nIn)
{
	firstRun=true;
    matcher.NumInputs=NumInputs;
    matcher.ITR_MAX =max_iter;
    matcher.step_control=true;
    matcher.n_neighbours=2;
    T.setIdentity();
    Td.setIdentity();
    max_size = *max_element(std::begin(size), std::end(size));

	map=new perception_oru::NDTMap ** [resolutions.size()];
	mapLocal=new perception_oru::NDTMap ** [resolutions.size()];
	mapLocal_prev=new perception_oru::NDTMap ** [resolutions.size()];
	for(unsigned int i=0;i<resolutions.size();i++)
	{
        map[i]= new perception_oru::NDTMap * [NumInputs];
        mapLocal[i]= new perception_oru::NDTMap * [NumInputs];
        mapLocal_prev[i]= new perception_oru::NDTMap * [NumInputs];
        float cur_res=resolutions.at(i);
        for(size_t j=0;j<NumInputs;j++)
        {
            perception_oru::LazyGrid *grid = new perception_oru::LazyGrid(cur_res);
            grid->semantic_index=j;
            map[i][j]=new perception_oru::NDTMap(grid);
            map[i][j]->guessSize(0,0,0,size[i],size[i],size[i]);
            map[i][j]->computeNDTCells(CELL_UPDATE_MODE_SAMPLE_VARIANCE);

            perception_oru::LazyGrid *grid2 = new perception_oru::LazyGrid(cur_res);
            grid2->semantic_index=j;
            mapLocal[i][j]=new perception_oru::NDTMap(grid2);
            mapLocal[i][j]->guessSize(0,0,0,size[i],size[i],size[i]);
            mapLocal[i][j]->computeNDTCells(CELL_UPDATE_MODE_SAMPLE_VARIANCE);

            perception_oru::LazyGrid *grid3 = new perception_oru::LazyGrid(cur_res);
            grid3->semantic_index=j;
            mapLocal_prev[i][j]=new perception_oru::NDTMap(grid3);
            mapLocal_prev[i][j]->guessSize(0,0,0,size[i],size[i],size[i]);
            mapLocal_prev[i][j]->computeNDTCells(CELL_UPDATE_MODE_SAMPLE_VARIANCE);
        }
	}
}
NDTMatch_SE::~NDTMatch_SE()
{
    for (std::map<int,perception_oru::NDTHistogram*>::iterator hist_it= key_hists.begin();
            hist_it != key_hists.end(); ++hist_it) 
        delete hist_it->second;
    for(unsigned int i=0;i<resolutions.size();i++)
    {
        for(auto j=0;j<NumInputs;j++)
        {
            delete map[i][j];
            delete mapLocal[i][j];
            delete mapLocal_prev[i][j];
        }
        delete[] map[i];
        delete[] mapLocal[i];
        delete[] mapLocal_prev[i];
    }
    delete[] map;
    delete[] mapLocal;
    delete[] mapLocal_prev;
}

#ifdef GL_VISUALIZE
void NDTMatch_SE::visualize()
{
    thread t1(&NDTMatch_SE::visualize_thread, this);
    t1.detach();
}
void NDTMatch_SE::visualize_thread()
{
    if(viewer==NULL)
    {
        viewer = new NDTViz(true);
        viewer->win3D->start_main_loop_own_thread();
    }
    float occupancy=0;
    std::vector<int > sem(NumInputs);
    std::iota(sem.begin(),sem.end(),0);
    std::vector<int > res=std::vector<int>{resolutions.size()-1};
    while(viewer->win3D->isOpen())
    {
         usleep(10000);
        if (viewer->win3D->keyHit())
        {
            int key = viewer->win3D->getPushedKey();
            switch (key){
                case '+':occupancy++;break;
                case '-':occupancy--;break;
                case '*':occupancy*=2;break;
                case '/':occupancy/=2;break;
                case 'q':delete viewer;break;
            }
            Eigen::Vector3d cpose = pose_graph.get();
            viewer->setCameraPointingToPoint(cpose(0),cpose(1),cpose(2));
            viewer->plotNDTSAccordingToOccupancy(occupancy, map,res, sem);
            viewer->addPoseGraph(pose_graph.forGL);
            viewer->win3D->keyHitReset();
        }
    }
}
#endif

int NDTMatch_SE::find_start(std::map<int,perception_oru::NDTHistogram*>::iterator pi, float max_size)
{
    Eigen::Vector3d cntr = pose_graph.get(pi->first);
    for(auto si=pi;si!=key_hists.begin();--si)
    {
        Eigen::Vector3d df = cntr-pose_graph.get(si->first);
        if((df.cwiseAbs().array()>max_size).any())
            return si->first;
    }
    return key_hists.begin()->first;
}

Eigen::Matrix<double, 7,7> NDTMatch_SE::getPoseInformation(Eigen::Affine3d T, perception_oru::NDTMap*** m1, perception_oru::NDTMap*** m2, bool inverse=false)
{
	Eigen::MatrixXd Covariance(6,6);
	matcher.covariance(m1,m2,T,resolutions,Covariance, true);
    Eigen::Vector3d ea = T.rotation().matrix().eulerAngles(0,1,2);
    Eigen::MatrixXd J=getJacobian(ea);
    Eigen::MatrixXd JI=Eigen::CompleteOrthogonalDecomposition<Eigen::MatrixXd>(J).pseudoInverse();
    Eigen::Matrix<double, 7,7> QCov=JI.transpose()*Covariance*JI; 
	return QCov;
}
bool NDTMatch_SE::matchToSaved(Eigen::Affine3d &Td_, Eigen::Vector3d &pose_ref, int start_index, int iP, Eigen::Matrix<double,7,7> &Ccl,int stop_index, int target_index)
{
    ofstream pose_out("LC.txt",std::ofstream::out|std::ofstream::app);
    bool result=true;
    perception_oru::NDTMap ***mapT;
	mapT=new perception_oru::NDTMap ** [resolutions.size()];
    for(unsigned int i=0;i<resolutions.size();i++)
    {
        mapT[i]= new perception_oru::NDTMap * [NumInputs];
            #pragma omp parallel num_threads(N_THREADS)
            {
                #pragma omp for
        for(unsigned int j=0;j<NumInputs;j++)
        {
            perception_oru::LazyGrid* grid = new perception_oru::LazyGrid(resolutions[i]);
            grid->semantic_index=j;
            mapT[i][j]=new perception_oru::NDTMap(grid,true);
            mapT[i][j]->guessSize(0,0,0,size[i],size[i],size[i]);
            //mapT[i][j]->computeNDTCells(CELL_UPDATE_MODE_SAMPLE_VARIANCE);
            mapT[i][j]->loadSaved(start_index, stop_index, pose_ref.data());
        }
            }
    }
    bool good_registration=true;
    for(auto i:resolutions_order)
    {
        matcher.current_resolution=resolutions.at(i);
        if(!matcher.match(mapT[i],map[i],Td_,true))
            good_registration=true;
        if(Td_.translation().norm()>(target_index-stop_index)*0.5)
        {
            good_registration=false;
        }
    }
    pose_out<<"Loop closed. Score: "<<matcher.score_best<< " START: "<<start_index<<" STOP: "<<stop_index<<"\n\t";
    if(matcher.score_best<COND_SCORE&&good_registration)
    {
        Ccl=getPoseInformation(Td, mapT,map,true);
//        CovSum=covS[iP]+Ccl;
        for(unsigned int i=0;i<resolutions.size();i++)
            #pragma omp parallel num_threads(N_THREADS)
            {
                #pragma omp for
                for(unsigned int j=0;j<NumInputs;j++)
                {
                    map[i][j]->transformNDTMap(Td_);
                }
            }
        pose_out<<"Transform :-) "<<Td_.translation().transpose()<<"\n\t";
    }
    else
    {
        if(!good_registration)
            pose_out<<"NOT GOOD. \n\t";
        pose_out<<"Transform :-( "<<Td_.translation().transpose()<<"\n\t";
        Td_.setIdentity();
        result=false;
    }
    for(unsigned int i=0;i<resolutions.size();i++)
    {
        #pragma omp parallel num_threads(N_THREADS)
        {
            #pragma omp for
            for(auto j=0;j<NumInputs;j++)
                delete mapT[i][j];
        }
        delete[] mapT[i];
    }
    delete[] mapT;
    return result;
}
#include <chrono>

Eigen::Affine3d NDTMatch_SE::slam(pcl::PointCloud<pcl::PointXYZI>::Ptr cloud)
{
    //Profiler pr;
	std::vector<pcl::PointCloud<pcl::PointXYZ>::Ptr >laserCloud=getSegmentsFast(cloud,NumInputs);
	for(unsigned int i=0;i<resolutions.size();i++)
		loadMap(mapLocal[i],laserCloud);
    Eigen::Affine3d T_prev= T;
    Eigen::Matrix<double,7,7> C;
	if(!firstRun)
	{
        for(auto i:resolutions_order)
        {
            matcher.current_resolution=resolutions.at(i);
            matcher.match(mapLocal_prev[i],mapLocal[i],Td,true);
        }
        double score_local = matcher.score_best;
        T=T_prev*Td;
        pose_graph.addPose(T,num_clouds);
        pose_graph.addConstraint(Td,num_clouds-1, num_clouds,
                getPoseInformation(Td,mapLocal_prev, mapLocal,true));
	}
	else{
        pose_graph.addPose(T,num_clouds);
        firstRun=false;
    }
    T_prev=T;
    perception_oru::NDTMap ***mapT;
    mapT=mapLocal_prev;
    mapLocal_prev=mapLocal;
    mapLocal=mapT;

    std::vector<thread> tc;
    for(size_t i=0;i<laserCloud.size();i++)
        tc.push_back( std::thread([i,&T_prev, laserCloud](){
                    perception_oru::transformPointCloudInPlace(T_prev, *laserCloud[i]);
                }));
    for(auto& t:tc)t.join();
    #pragma omp parallel num_threads(N_THREADS)
    {
        #pragma omp for
        for(size_t i=0;i<laserCloud.size();i++)
        {
            for(int rez=0;rez<resolutions.size();rez++)
            {
                map[rez][i]->addPointCloud(T.translation(), *laserCloud[i],0.06, 100, 0.03, 255);
                map[rez][i]->computeNDTCells(CELL_UPDATE_MODE_SAMPLE_VARIANCE,1e9,255,T.translation(),0.01);
            }
        }
    }

    perception_oru::NDTHistogram *hist = new perception_oru::NDTHistogram (map[1], 1, HIST_BINS1, HIST_BINS2, NumInputs,RANGE1, RANGE2);
    if(key_hists.size()==0){
        key_hists[num_clouds]=hist;
        num_clouds++;
        return T;
    }
    ofstream pose_out("LC.txt",std::ofstream::out|std::ofstream::app);
    ///LOOP CLOSE SEARCH
    std::vector<int> poseIdxSearch;
    float search_distance=HIST_ACCURATE_DISTANCE+(num_clouds -last_loop_close_id)*0.10;

    poseIdxSearch=pose_graph.get_in_range<perception_oru::NDTHistogram*>(key_hists,search_distance,100);
    if(poseIdxSearch.size()>0)
    {
        std::vector<int> hist_id;
        std::vector<Eigen::Affine3d, Eigen::aligned_allocator<Eigen::Affine3d> >
            hist_rotations;
        std::mutex result_mutex;
        #pragma omp parallel num_threads(N_THREADS)
        {
            #pragma omp for
            for(std::vector<int>::iterator it=poseIdxSearch.begin();
                    it!=poseIdxSearch.end();++it)
            {
                Eigen::Affine3d tmp_rotation=Eigen::Affine3d::Identity();
                key_hists[*it]->bestFitToHistogram(*hist,tmp_rotation,false);
                double histogram_score = key_hists[*it]->getSimilarity(*hist,tmp_rotation);
                if(histogram_score<=SIMILARITY_THRESHOLD)
                {
                    const std::lock_guard<std::mutex> lock(result_mutex);
                    hist_rotations.push_back(tmp_rotation);
                    hist_id.push_back(*it);
                    std::cout<<histogram_score<<std::endl;
                }
            }
        }
        for(int i=0;i<hist_id.size();i++)
        {
            int id = hist_id[i];
            int loadStartIndex=find_start(key_hists.find(id), max_size);
            Eigen::Matrix<double, 7,7> Ccl;
            T_prev=pose_graph.getT(id).inverse()*T;
            T_prev.matrix().block<3,3>(0,0)=hist_rotations[i].matrix().block<3,3>(0,0);
            Eigen::Vector3d pose_ref = pose_graph.get(id);
            bool matched= matchToSaved(T_prev,pose_ref, loadStartIndex, id, Ccl,id,num_clouds);
            if(matched)
            {
                pose_graph.addConstraint(T_prev,id, num_clouds,Ccl);
                pose_graph.solve();
                T=pose_graph.getT();
                last_loop_close_id=num_clouds;
            }
        }
    }
    if(pose_graph.distance(key_hists.rbegin()->first)>HIST_FREQ_METER)
    {
        key_hists[num_clouds]=hist;
    }
    num_clouds++;
    //pr.elapsed(0);
    pose_out.close();
	return T;
}


