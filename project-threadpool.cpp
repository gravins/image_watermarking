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

using namespace cimg_library;
using namespace std;
using namespace std::chrono;

vector<CImg<int>> *img_vec = new vector<CImg<int>>();
CImg<int> watermark;
atomic<int> run(1);
int end_task = 0;
int start_task = 0;
mutex mtx_queue;
mutex mtx_end;
condition_variable cv_queue;
condition_variable cv_end;
time_point<high_resolution_clock> start_time;
time_point<high_resolution_clock> end_time;
atomic<int> counter(0);

auto s_thread = high_resolution_clock::now();

struct task{
    int fun;
    const char * img_path;
    int start_x;
    int start_y;
    int end_x;
    int end_y;
    CImg<int> * src;
};

// Define queue of task
queue<struct task *> task_list;


/**
    Function to load an image.
    @param imgs_path: path of image to load
*/
void load_img(const char * img_path){

    //cout << "Open: " <<  img_path << endl;
    CImg<int> src;
    int error = 0;
    try{
        src.load_jpeg(img_path);
    }
    catch (const cimg_library::CImgIOException& e){
        // Try again to load image
        try{
            src.load_jpeg(img_path);
        }
        catch (const cimg_library::CImgIOException& e){
            try{
                src.load_jpeg(img_path);
            }
            catch (const cimg_library::CImgIOException& e){
                cout <<  "Cannot open " << img_path << ". Image was discarded."<< endl;
                error = 1;
            }
        }
    }

    if (!error){
        if (!watermark.is_sameXY(src)){
            cout << "Different size for " << img_path << " : Image was discarded."<< endl;
        }
        else{
            // Append image vector to global image vecotr
            {
                unique_lock<mutex> lock_queue(mtx_queue);
                img_vec->push_back(src);
            }
        }
    }
}


/**
    Function that apply watermark to small region of the image.
    @param start_x: x of the starting point
    @param start_y: y of the starting point
    @param end_x: x of the endinging point
    @param end_y: y of the endinging point
    @param src: reference to the original image

*/
void app_watermark(int start_x, int start_y, int end_x, int end_y, CImg<int> * src){
    if (end_x > watermark.width()){
        end_x = watermark.width() - 1;
    }
    if (end_y > watermark.height()){
        end_y = watermark.height() - 1;
    }

    int v;
    for (int i = start_x; i <= end_x; i++) {
        for (int j = start_y; j <= end_y; j++) {
            v = watermark(i, j, 0, 0);
            if (v != 255 ){
                (*src)(i,j,0,0) = 0;
                (*src)(i,j,0,1) = 0;
                (*src)(i,j,0,2) = 0;
            }
        }
    }

}


/**
    Function to save one image.
    @param imgs: image to save
    @param path: path of the image
*/
void save_img(CImg<int> * img, const char * path, int i){
    string string_path(path);
    string_path += "/watermark_" + to_string(i) + ".png";
    img->save_png(string_path.c_str());
}


/**
    Thread job
*/
void thread_fun(){
    struct task * t;
    while(run.load() != 0){
        {
            unique_lock<mutex> lock_queue(mtx_queue);
            while (task_list.size() <= 0 && run.load() != 0) {
                cv_queue.wait(lock_queue);
            }
            if (task_list.size() > 0){
                t = task_list.front();
                task_list.pop();
            }
            if (start_task == 0){
                start_task += 1;
                start_time = high_resolution_clock::now();
            }
        }
        
        if (run.load() != 0 ){
            switch (t->fun) {
                case (0):
                    load_img(t->img_path);
                    break;
                case (1):
                    app_watermark(t->start_x, t->start_y, t->end_x, t->end_y, t->src);
                    break;
                case (2):
                    save_img(t->src, t->img_path, t->start_x);
                    break;
            }
            end_time = high_resolution_clock::now();

            // Notify to Main that task end
            {
                unique_lock<mutex> lock_end(mtx_end);
                end_task++;
                cv_end.notify_one();
            }
            delete t;

        }
    }
}


int main(int argc, char* argv[]){
    // Check the number of parameters
    if (argc < 3 || argc > 5){
        // Tell the user how to run the program
        cerr << "Usage: " << argv[0] << " path/of/the/images  path/of/jpg_watermark [optional]strategy [optional]parallelism_degree" << endl;
        cerr << "strategy: default row by row\n\t0 = work image by image \n\t1 = work some rows per thread \n\t2 = work pixel by pixel " << endl;
        cerr << "parallel_degree: default is 4" << endl;
        return 1;
    }

    // Read parameters
    string img_path;
    const char * watermark_path;
    int par_deg;
    int strategy;

    img_path = argv[1];
    img_path = img_path[img_path.length() - 1] == '/' ? img_path : img_path+'/';
    watermark_path = argv[2];
    strategy = (argc > 3) ? atoi(argv[3]) : 1;
    if (strategy != 0 && strategy != 1 && strategy != 2 ){
        cerr << "strategy: \n\t0 = work image by image \n\t1 = work some rows per thread \n\t2 = work pixel by pixel" << endl;
        return 1;
    }
    par_deg = (argc == 5) ? atoi(argv[4]) : 4;

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
        if (strcmp(d_name,"result") == 0){
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
        auto result_path = img_path + "result";
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


    int w_width = watermark.width();
    int w_height = watermark.height();
    cout << "Watermark loaded, size " << w_width << "x" << w_height << endl;

    // Create the vector of thread that handle the images
    vector<thread> *t_pool = new vector<thread>();
    s_thread = high_resolution_clock::now();
    for (int i = 0; i < par_deg; i++){
        t_pool->push_back(thread(thread_fun));
    }

    // Set flag for the first thread that work on the job to 0
    {
        unique_lock<mutex> lock_queue(mtx_queue);
        start_task = 0;
    }
    // Load images in parallel
    auto start = high_resolution_clock::now();
    for (size_t i = 0; i < img_files->size(); i++) {
        struct task * t = new struct task;
        t->fun = 0;
        t->img_path = (const char *) (*img_files->at(i)).c_str();
        {
            unique_lock<mutex> lock_queue(mtx_queue);
            task_list.push(t);
            cv_queue.notify_one();
        }
        counter.fetch_add(1);
        
    }

    // Wait until all images are loaded
    {
        unique_lock<mutex> lock_end(mtx_end);
        while (end_task != counter) {
            cv_end.wait(lock_end);
        }
    }
    auto end = high_resolution_clock::now();
    auto us = duration_cast<microseconds>(end-start).count();
    auto sec = (float)us / (float)1000000;
    auto us_real = duration_cast<microseconds>(end_time-start_time).count();
    auto sec_real = (float)us_real / (float)1000000;
    cout << "Load " << counter <<" images required effectively:" << us_real << " microsec = " << sec_real << " sec" << endl;
    cout << "Load " << counter <<" images required :" << us << " microsec = " << sec << " sec" << endl;

    // Set counter of finisched job to 0
    {
        unique_lock<mutex> lock_end(mtx_end);
        end_task = 0;
    }
    // Set flag for the first thread that work on the job to 0
    {
        unique_lock<mutex> lock_queue(mtx_queue);
        start_task = 0;
    }

    // Apply watermarks
    int x, y, x_end, y_end;
    int row_per_thread, remaining_row,remaining_iter;
    switch (strategy) {
        case 0:
            // Case in which we manage image by image
            counter.store(0);
            start = high_resolution_clock::now();

            // For every image
            for (size_t i = 0; i < img_vec->size(); i++){
                // Apply the watermark
                struct task * t = new struct task;
                t->fun = 1;
                t->start_x = 0;
                t->start_y = 0;
                t->end_x = w_width - 1;
                t->end_y = w_height - 1;
                t->src = &img_vec->at(i);

                {
                    unique_lock<mutex> lock_queue(mtx_queue);
                    task_list.push(t);
                    cv_queue.notify_one();
                }
                counter.fetch_add(1);

            }

            break;

        case 1:
            // Case in which we manage the image with a set of rows per thread
            row_per_thread = max(1, w_height / par_deg);
            remaining_row = w_height - (row_per_thread * par_deg);
            remaining_iter = ceil((float)remaining_row / (float)row_per_thread);

            x = 0, y = 0;
            x_end = w_width - 1;
            y_end = row_per_thread - 1;

            counter.store(0);
            start = high_resolution_clock::now();

            // For every image
            for (size_t i = 0; i < img_vec->size(); i++){
                // For every thread assign the specific area in which operate
                for (int j = 0; j < par_deg + remaining_iter ; j++) {
                    // Apply the watermark
                    struct task * t = new struct task;
                    t->fun = 1;
                    t->start_x = x;
                    t->start_y = y;
                    t->end_x = x_end;
                    t->end_y = y_end;
                    t->src = &img_vec->at(i);
                    {
                        unique_lock<mutex> lock_queue(mtx_queue);
                        task_list.push(t);
                        cv_queue.notify_one();
                    }
                    counter.fetch_add(1);

                    // Move the area for the next thread
                    y = y_end;
                    y_end += row_per_thread;

                }
                // Reinitialize to first area for next image
                x = 0, y = 0;
                x_end = w_width - 1;
                y_end = row_per_thread - 1;
            }

            break;

        default:
            // Case in which we manage the image pixel by pixel
            x = 0, y = 0, x_end = 0, y_end = 0;
            counter.store(0);
            start = high_resolution_clock::now();

            for (size_t i = 0; i < img_vec->size(); i ++){
                for (int j = 0; j < w_height * w_width; j++) {
                    // Apply the watermark to the image, specific to the area menaged from the specific thread
                    struct task * t = new struct task;
                    t->fun = 1;
                    t->start_x = x;
                    t->start_y = y;
                    t->end_x = x_end;
                    t->end_y = y_end;
                    t->src = &img_vec->at(i);
                    {
                        unique_lock<mutex> lock_queue(mtx_queue);
                        task_list.push(t);
                        cv_queue.notify_one();
                    }
                    counter.fetch_add(1);

                    // Move the area for the next thread
                    // Move of one pixel
                    x_end = (x_end + 1 >= w_width) ? 0 : x_end+1;
                    x = x_end;
                    y_end = (x_end == 0) ? y_end + 1 : y_end;
                    y = y_end;
                }

                // Reinitialize to first area for next image
                x = 0;
                y = 0;
                x_end = 0;
                y_end = 0;
            }

            break;
    }

    // Wait until all images are processed
    {
        unique_lock<mutex> lock_end(mtx_end);
        while (end_task != counter) {
            cv_end.wait(lock_end);
        }
    }
    end = high_resolution_clock::now();
    us = duration_cast<microseconds>(end-start).count();
    sec = (float)us / (float)1000000;
    us_real = duration_cast<microseconds>(end_time-start_time).count();
    sec_real = (float)us_real / (float)1000000;
    cout << "Apply watermark required effectively:" << us_real << " microsec = " << sec_real << " sec" << endl;
    cout << "Apply watermark required :" << us << " microsec = " << sec << " sec" << endl;


    // Set counter of finisched job to 0
    {
        unique_lock<mutex> lock_end(mtx_end);
        end_task = 0;
    }
    // Set flag for the first thread that work on the job to 0
    {
        unique_lock<mutex> lock_queue(mtx_queue);
        start_task = 0;
    }
    // Save images in parallel
    counter.store(0);
    string out_path = img_path + "result";
    start = high_resolution_clock::now();
    for (size_t i = 0; i < img_vec->size(); i++){
        struct task * t = new struct task;
        t->fun = 2;
        t->src = &img_vec->at(i);
        t->img_path = (const char *) out_path.c_str();
        t->start_x = i;

        {
            unique_lock<mutex> lock_queue(mtx_queue);
            task_list.push(t);
            cv_queue.notify_one();
        }
        counter.fetch_add(1);
    }
    // Wait until all images are loaded
    {
        unique_lock<mutex> lock_end(mtx_end);
        while (end_task != counter) {
            cv_end.wait(lock_end);
        }
    }
    end = high_resolution_clock::now();
    us = duration_cast<microseconds>(end-start).count();
    sec = (float)us / (float)1000000;
    us_real = duration_cast<microseconds>(end_time-start_time).count();
    sec_real = (float)us_real / (float)1000000;
    cout << "Save images required effectively:" << us_real << " microsec = " << sec_real << " sec" << endl;
    cout << "Save images required :" <<  us << " microsec = " << sec << " sec" << endl;

    // Terminate all thread in the pool and free memory
    run--;
    cv_queue.notify_all();
    for (size_t i = 0; i < t_pool->size(); i++) {
        t_pool->at(i).join();
    }

    delete img_vec;
    for (size_t i = 0; i < img_files->size(); i++) {
        delete img_files->at(i);
    }
    delete img_files;
    delete t_pool;

    return 0;
}
