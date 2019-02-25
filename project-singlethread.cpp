#include <iostream>
#include <vector>
#include <string>
#include <math.h>
#include <chrono>

#include "CImg.h"

using namespace cimg_library;
using namespace std;
using namespace std::chrono;

vector<CImg<int>> *img_vec = new vector<CImg<int>>();
CImg<int> watermark;


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
            img_vec->push_back(src);
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
void save_img(CImg<int> * img, const char * path){
    img->save_png(path);
}




int main(int argc, char* argv[]){
    // Check the number of parameters
    if (argc != 3){
        // Tell the user how to run the program
        cerr << "Usage: " << argv[0] << " path/of/the/images  path/of/jpg_watermark" << endl;
        return 1;
    }

    // Read parameters
    string img_path;
    const char * watermark_path;

    img_path = argv[1];
    img_path = img_path[img_path.length() - 1] == '/' ? img_path : img_path+'/';
    watermark_path = argv[2];


    // Read all file into images directory
    DIR* dir;
    dirent* pdir;
    vector<string> *img_files = new vector<string>();


    if ((dir = opendir(argv[1])) == NULL){
        cerr << "Cannot open " << img_path << endl;
        return 1;
    }

    errno = 0;
    int dir_exist = 0;
    while ((pdir = readdir(dir)) != NULL) {
        const char * d_name = pdir->d_name;
        if (strcmp(d_name,".") != 0 && strcmp(d_name,".directory") != 0 && strcmp(d_name,"..") != 0 && strcmp(d_name,"result") != 0 && strcmp(d_name,"result_ff") != 0){
            img_files->push_back(img_path + d_name);
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

    // Load images
    auto start = high_resolution_clock::now();
    for (size_t i = 0; i < img_files->size(); i++) {
        load_img((const char *)img_files->at(i).c_str());
    }

    auto end = high_resolution_clock::now();
    auto us = duration_cast<microseconds>(end-start).count();
    auto s = (float)us / (float)1000000;
    cout << "Load " << img_files->size() <<" images required :" << us << " microsec = " << s << " sec" << endl;


    // Apply watermarks
    start = high_resolution_clock::now();

    // For every image
    for (size_t i = 0; i < img_vec->size(); i++){
        app_watermark(0, 0, w_width - 1, w_height - 1, &img_vec->at(i));
    }

    end = high_resolution_clock::now();
    us = duration_cast<microseconds>(end-start).count();
    s = (float)us / (float)1000000;
    cout << "Apply watermark required :" << us << " microsec = " << s << " sec" << endl;

    // Save images in parallel
    start = high_resolution_clock::now();

    for (size_t i = 0; i < img_vec->size(); i++){
        string s = img_path + "result/watermark"+ to_string(i) + ".png";
        save_img(&img_vec->at(i), (const char *) s.c_str());
    }

    end = high_resolution_clock::now();
    us = duration_cast<microseconds>(end-start).count();
    s = (float)us / (float)1000000;
    cout << "Save images required :" <<  us << " microsec = " << s << " sec" << endl;


    delete img_vec;
    delete img_files;

    return 0;
    }
