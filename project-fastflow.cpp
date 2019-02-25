#include <iostream>
#include <vector>
#include <string>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <queue>
#include <math.h>
#include <chrono>
#include <atomic>

#include "CImg.h"
#include <ff/farm.hpp>
#include <ff/pipeline.hpp>


using namespace ff;
using namespace cimg_library;
using namespace std;
using namespace std::chrono;

CImg<int> watermark;
int par_deg;
int strategy;
string img_path;
time_point<high_resolution_clock>  start_load_time;
time_point<high_resolution_clock>  end_load_time;
time_point<high_resolution_clock>  start_apply_time;
time_point<high_resolution_clock>  end_apply_time;
time_point<high_resolution_clock>  start_save_time;
time_point<high_resolution_clock>  end_save_time;


struct task{
    string * img_path;
    int start_x = 0;
    int start_y = 0;
    int end_x = 0;
    int end_y = 0;
    CImg<int> * src;
    atomic<int> * count; // specify how many subtasks are created
};


// Define first stage of the pipeline (LOAD images)
// Define emitter of the first farm
struct Emitter_load: ff_node_t<task> {
    Emitter_load(vector<string*> *paths): paths(paths) {}

    task * svc(task * t) {
        start_load_time = high_resolution_clock::now();
        for(size_t i = 0; i < paths->size(); i++) {
            struct task * t = new struct task;
            t->img_path = paths->at(i);
            ff_send_out(t);
        }

        return EOS;

    }

    vector<string*> *paths;
};

struct Worker_load: ff_node_t<task>{
    task * svc(task * t){
        int error = 0;
        t->src = new CImg<int>();
        try{
            t->src->load_jpeg(t->img_path->c_str());
        }
        catch (const cimg_library::CImgIOException& e){
            // Try again to load image
            try{
                t->src->load_jpeg(t->img_path->c_str());
            }
            catch (const cimg_library::CImgIOException& e){
                try{
                    t->src->load_jpeg(t->img_path->c_str());
                }
                catch (const cimg_library::CImgIOException& e){
                    cout <<  "Cannot open " << *(t->img_path) << ". Image was discarded."<< endl;
                    error = 1;
                }
            }
        }

        if (!error){
            if (watermark.is_sameXY(*t->src)){
                end_load_time = high_resolution_clock::now();
                ff_send_out(t);
            }
            else{
                cout << "Different size for " << *(t->img_path) << " : Image was discarded."<< endl;
                delete t->src;
                delete t;
            }
        }
        else{
            delete t->src;
            delete t;
        }

        return GO_ON;
    }
};


// Define second stage of the pipeline (APPLY watermark)
// Define emitter of the second farm
struct Emitter_apply: ff_node_t<task> {
    task * svc(task * t) {
        if (time){
            start_apply_time = high_resolution_clock::now();
            time = 0;
        }

        int x, y, x_end, y_end;
        switch (strategy){
            case 0:
                // image by image
                t->count = new atomic<int>(1);

                t->end_x = watermark.width();
                t->end_y = watermark.height();
                ff_send_out(t);
                break;
            case 1:
                // row by row
                t->count = new atomic<int>(watermark.height());
                for (int j = 0; j < watermark.height(); j++) {
                    struct task * t2 = new struct task;
                    t2->count = t->count;
                    t2->img_path = t->img_path;
                    t2->src = t->src;
                    t2->end_x = watermark.width();
                    t->end_y += 1;
                    t2->end_y = t->end_y;
                    ff_send_out(t2);
                }
                delete t;
                break;

        }
        return GO_ON;
    }

    int time = 1;
};

struct Worker_apply: ff_node_t<task>{
    task * svc(task * t){
        int v;
        for (int i = t->start_x; i < t->end_x; i++) {
            for (int j = t->start_y; j < t->end_y; j++) {
                v = watermark(i, j, 0, 0);
                if (v != 255 ){
                    (*t->src)(i,j,0,0) = 0;
                    (*t->src)(i,j,0,1) = 0;
                    (*t->src)(i,j,0,2) = 0;
                }
            }
        }
        if ((t->count->fetch_sub(1) - 1) == 0){
            ff_send_out(t);
        }
        else{
            delete t;
        }
        end_apply_time = high_resolution_clock::now();
        return GO_ON;
    }
};


// Define third stage of the pipeline (SAVE images)
// Define emitter of the third farm
struct Emitter_save: ff_node_t<task> {
    task * svc(task * t) {
        if (time){
            start_save_time = high_resolution_clock::now();
            time = 0;
        }

        ff_send_out(t);
        return GO_ON;
    }

    int time = 1;
};

atomic<int> num(0);
struct Worker_save: ff_node_t<task>{
    task * svc(task * t){
        int n = num.fetch_add(1);
        string string_path = img_path + "result_ff/watermark_" + to_string(n) + ".png";
        (t->src)->save_png(string_path.c_str());

        end_save_time = high_resolution_clock::now();

        delete t->count;
        delete t->src;
        delete t;

        return GO_ON;
    }
};



int main(int argc, char *argv[]) {
     // Check the number of parameters
    if (argc < 3 || argc > 5){
        // Tell the user how to run the program
        cerr << "Usage: " << argv[0] << " path/of/the/images  path/of/jpg_watermark [optional]strategy [optional]parallelism_degree" << endl;
        cerr << "strategy: default row by row\n\t0 = work image by image \n\t1 = work some rows per thread " << endl;
        cerr << "parallel_degree: default is 4" << endl;
        return 1;
    }

    // Read parameters
    const char * watermark_path;

    img_path = argv[1];
    img_path = img_path[img_path.length() - 1] == '/' ? img_path : img_path+'/';
    watermark_path = argv[2];

    strategy = (argc > 3) ? atoi(argv[3]) : 1;
    if (strategy != 0 && strategy != 1 ){
        cerr << "strategy: \n\t0 = work image by image \n\t1 = work some rows per thread "<<endl;
        return 1;
    }

    par_deg = (argc == 5) ? max(3, atoi(argv[4])) : 4; // minimum parallelism degree is 3
    if (atoi(argv[4]) < 3){
        cout << "The chosen parallelism degree is to low,\n\t parallelism degree set to 3" << endl;
    }
    int load_deg = par_deg / 3;
    int mark_deg = load_deg;
    int save_deg = par_deg - (load_deg * 2);

    // Read all file into images directory
    DIR* dir;
    dirent* pdir;
    vector<string*> *img_files = new vector<string*>();

    if ((dir = opendir(argv[1])) == NULL){
        cerr << "Cannot open " << img_path << endl;
        return 1;
    }

    errno = 0;
    int dir_exist = 0;
    while ((pdir = readdir(dir)) != NULL) {
        const char * d_name = pdir->d_name;
        if (strcmp(d_name,".") != 0 && strcmp(d_name,".directory") != 0 && strcmp(d_name,"..") != 0 && strcmp(d_name,"result") != 0 && strcmp(d_name,"result_ff") != 0){
            string * in_path = new string(img_path + d_name);
            img_files->push_back(in_path);
        }
        if (strcmp(d_name,"result_ff") == 0){
            dir_exist = 1;
        }
    }

    if (errno != 0){
        cerr << "Error handling " << img_path << ", errno= " << errno << endl;
        return errno;
    }

    closedir(dir);
    if (errno != 0){
        cerr << "Error closing " << img_path << ", errno= " << errno << endl;
        return errno;
    }

    if (!dir_exist){
        int stat;
        errno = 0;
        // Create directory for save images with watermark with
        // user, group and others are able to Read + Write + Execute
        auto result_path = img_path + "result_ff";
        stat = mkdir(result_path.c_str(), S_IRWXU | S_IRWXG | S_IRWXO);
        if (!stat){
            cout << "Create directory to save images with watermark" << endl;
        }
        else{
            cerr << "Unable to create directory to save images with watermark" << endl;
            return errno;
        }
    }

    // Load watermark, if exist
    try{
        watermark.load_jpeg(watermark_path);
    }
    catch (const cimg_library::CImgIOException& e){
        cout <<  "Cannot open watermark"<< endl;
        return 1;
    }

    cout << "Watermark loaded, size " << watermark.width() << "x" << watermark.height() << endl;

    // Create the first stage of the pipeline
    Emitter_load stage1_e(img_files);
    std::vector<std::unique_ptr<ff_node>> Workers_1;
    for (int i = 0; i < load_deg; i++) {
        Workers_1.push_back( make_unique<Worker_load>() );
    }
    ff_Farm<task> farm1(move(Workers_1), stage1_e);
    farm1.remove_collector();
    //farm1.set_scheduling_ondemand();

    // Create the second stage of the pipeline
    Emitter_apply stage2_e;
    std::vector<std::unique_ptr<ff_node>> Workers_2;
    for (int i = 0; i < mark_deg; i++) {
        Workers_2.push_back(make_unique<Worker_apply>());
    }
    ff_Farm<task> farm2(move(Workers_2), stage2_e);
    farm2.remove_collector();
    farm2.setMultiInput();
    //farm2.set_scheduling_ondemand();

    // Create the third stage of the pipeline
    Emitter_save stage3_e;
    std::vector<std::unique_ptr<ff_node>> Workers_3;
    for (int i = 0; i < save_deg; i++) {
        Workers_3.push_back(make_unique<Worker_save>());
    }
    ff_Farm<task> farm3(move(Workers_3), stage3_e);
    farm3.remove_collector();
    farm3.setMultiInput();
    //farm3.set_scheduling_ondemand();

    ff_Pipe<task> pipe(farm1, farm2, farm3);

    auto st = high_resolution_clock::now();
    if (pipe.run_and_wait_end() < 0){
        std::cerr << "Error running pipe" << std::endl;
        return 1;
    }
    auto et = high_resolution_clock::now();
    auto us = duration_cast<microseconds>(et-st).count();
    auto sec = (float)us / (float)1000000;
    cout << "Load + Apply_watermark +Save required :" << us << " microsec = " << sec << " sec" << endl;

    us = duration_cast<microseconds>(end_load_time-start_load_time).count();
    sec = (float)us / (float)1000000;
    cout << "Load " << img_files->size() <<" images required :" << us << " microsec = " << sec << " sec" << endl;

    us = duration_cast<microseconds>(end_apply_time-start_apply_time).count();
    sec = (float)us / (float)1000000;
    cout << "Apply watermark required :" << us << " microsec = " << sec << " sec" << endl;

    us = duration_cast<microseconds>(end_save_time-start_save_time).count();
    sec = (float)us / (float)1000000;
    cout << "Save images required :" <<  us << " microsec = " << sec << " sec" << endl;


    // Free memory
    for (size_t i = 0; i < img_files->size(); i++) {
        delete img_files->at(i);
    }
    delete img_files;

    return 0;
}
