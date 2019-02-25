Standard C++ and [FastFlow](https://github.com/fastflow/fastflow) implementation of image watermarking.

**Usage**
- `make`
- single thread: `./out-st img/ watermark.jpg`
- threadpool: `./out-tp img/ watermark.jpg [strategy] [par degr]`
- fasttext: `./out-ff img/ watermark.jpg [strategy] [par degr]`

[strategy]:
- 0 : one image per thread
- 1 : max(1, image_height / par_deg) rows per thread
- 2 : one pixel per thread