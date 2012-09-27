#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "vf_dlopen.h"

/*
 * test image
 *
 * usage: -vf dlopen=./test1.so
 *
 * a simple test image generator
 */

#define PLANE_Y 0
#define PLANE_U 1
#define PLANE_V 2

#define MIN(a,b) (((a)<(b)) ? (a) : (b))

typedef struct
{
    int gridspacing_x, gridspacing_y;
    unsigned circle_x1, circle_y1, circle_x2, circle_y2;
    double circle_mx, circle_my, circle_rx, circle_ry, circle_rx2, circle_ry2;
    unsigned box_x1, box_y1, box_x2, box_y2;
}
privdata;

static int config(struct vf_dlopen_context *ctx)
{
    privdata *priv = ctx->priv;

    priv->gridspacing_x = 64 * ctx->in_width / ctx->in_d_width;
    priv->gridspacing_y = 64 * ctx->in_height / ctx->in_d_height;

    double circle_r = (MIN(ctx->in_d_width, ctx->in_d_height) * 7) / 16.0;
    priv->circle_rx = circle_r * ctx->in_width / ctx->in_d_width;
    priv->circle_ry = circle_r * ctx->in_height / ctx->in_d_height;

    priv->circle_rx2 = (circle_r - 2) * ctx->in_width / ctx->in_d_width;
    priv->circle_ry2 = (circle_r - 2) * ctx->in_height / ctx->in_d_height;
    priv->circle_mx = (ctx->in_width - 1) / 2.0;
    priv->circle_my = (ctx->in_height - 1) / 2.0;
    priv->circle_x1 = floor(priv->circle_mx - priv->circle_rx) - 1;
    priv->circle_y1 = floor(priv->circle_my - priv->circle_ry) - 1;
    priv->circle_x2 = ceil(priv->circle_mx + priv->circle_rx) + 1;
    priv->circle_y2 = ceil(priv->circle_my + priv->circle_ry) + 1;

    priv->box_x1 = (ctx->in_width - ((int) priv->circle_rx * 1.5)) / 2;
    priv->box_y1 = (ctx->in_height - ((int) priv->circle_ry * 1.5)) / 2;
    priv->box_x2 = ctx->in_width - 1 - priv->box_x1;
    priv->box_y2 = ctx->in_height - 1 - priv->box_y1;

    return 1;
}

//#define CRAPPY
double ellipse_box_intersection(double x1, double y1, double x2, double y2, double mx, double my, double rx, double ry)
{
    double xx = (x1 + x2) / 2;
    double yy = (y1 + y2) / 2;
    double dx = mx - xx;
    double dy = my - yy;
    double w = x2 - x1;
    double h = y2 - y1;

    // scale aspect so we can think in terms of circles
    double r = sqrt(rx * ry);
    double fx = r / rx;
    double fy = r / ry;
    dx *= fx;
    w *= fx;
    dy *= fy;
    h *= fy;

    double dist2 = dx * dx + dy * dy;

#ifdef CRAPPY
    if(dist2 > r * r)
        return 0;
    else
        return w * h;
#else
    // FIXME implement real box-circle intersection
    // it is complicated
    // so I did circle-circle intersection instead

    // circle of same area
    double rr = sqrt(w * h / M_PI);
    double dist = sqrt(dist2);
    if (dist >= rr + r)
        return 0;
    else if (dist <= rr - r)
        return M_PI * r * r;
    else if (dist <= r - rr)
        return M_PI * rr * rr;
    else
        return
            r * r * acos((dist * dist + r * r - rr * rr) / (2 * dist * r))
            +
            rr * rr * acos((dist * dist + rr * rr - r * r) / (2 * dist * rr))
            -
            0.5 * sqrt((-dist + r + rr) * (dist + r - rr) * (dist - r + rr) * (dist + r + rr));
#endif
}

#define BLACK 16
#define WHITE 235
#define GREY 128

// colors 75% and 0% RGB values only
static unsigned char colors[7][3] = {
    { 180, 128, 128 }, // grey
    { 162, 44, 142 }, // yellow
    { 131, 156, 44 }, // cyan
    { 112, 72, 58 }, // green
    { 84, 184, 198 }, // magenta
    { 65, 100, 212 }, // red
    { 35, 212, 114 } // blue
};

// Test bar for TV levels
static unsigned char greys[8] = {
    0, // maximum black
    BLACK - 10, // saturated black minus one
    BLACK, // saturated black
    BLACK + 10, // saturated black plus one
    WHITE - 10, // saturated white minus one
    WHITE, // saturated white
    WHITE + 10, // whiter than white
    255 // maximum white
};

char lcd[9][5] = {
    " 000 ",
    "1   2",
    "1   2",
    "1   2",
    " 333 ",
    "4   5",
    "4   5",
    "4   5",
    " 666 "
};

char digits[13] = {
    0167, // 0
    0044, // 1
    0135, // 2
    0155, // 3
    0056, // 4
    0153, // 5
    0173, // 6
    0047, // 7
    0177, // 8
    0157, // 9
    0100, // .
    0110, // :
    0010  // -
};

static unsigned char testimage_pixel(struct vf_dlopen_context *ctx, int px, int py, const char *ptsbuf, unsigned p, unsigned x, unsigned y, unsigned char c)
{
    privdata *priv = ctx->priv;
    unsigned char newc;

    if(p) {
        // medium UV value
        newc = GREY;
    } else {
        // initialize to black
        newc = BLACK;

        // grid
        {
            int ax = ((int) x) - px;
            int ay = ((int) y) - py;
            if (ax == 0 || ax == 1)
                newc = WHITE;
            else if (ay == 0 || ay == 1)
                newc = WHITE;
            else if (abs(ax) % priv->gridspacing_x <= 1)
                newc = GREY;
            else if (abs(ay) % priv->gridspacing_y <= 1)
                newc = GREY;
        }
    }

    // blend
    newc = (newc + (unsigned int) c) >> 1;

    // box
    if(x >= priv->box_x1 && x <= priv->box_x2 && y >= priv->box_y1 && y <= priv->box_y2) {
        int width = priv->box_x2 + 1 - priv->box_x1;
        int height = priv->box_y2 + 1 - priv->box_y1;

        // total size: 99 rows, 112 columns
        int col = (x - priv->box_x1) * 112 / width;
        int row = (y - priv->box_y1) * 99 / height;

        newc = GREY;

        switch(row / 11)
        {
            case 0:
            case 1:
                // color bars
                if (row >= 19)
                    if ((col / 16) % 2)
                        newc = p ? GREY : BLACK;
                    else
                        newc = colors[6 - (col / 32) * 2][p];
                else
                    newc = colors[col / 16][p];
                break;
            case 2:
                // grey bars
                if (!p)
                    newc = greys[col / 14];
                break;
            case 3:
                // text (rendered pts)
                if (!p) {
                    int str_y = row - 3 * 11 - 1;
                    int chr = col / 7;
                    int chr_x = col % 7 - 1;
                    if (chr_x != -1 && chr_x != 5 && str_y != -1 && str_y != 9) {
                        int segment = lcd[str_y][chr_x];
                        if (segment >= '0' && segment <= '7') {
                            char digit = ptsbuf[chr];
                            if (digit >= '0' && digit <= '9')
                                newc = (digits[digit - '0'] & (1 << (segment - '0'))) ? WHITE : BLACK;
                            else if (digit == '.')
                                newc = (digits[10] & (1 << (segment - '0'))) ? WHITE : BLACK;
                            else if (digit == ':')
                                newc = (digits[11] & (1 << (segment - '0'))) ? WHITE : BLACK;
                            else if (digit == '-')
                                newc = (digits[12] & (1 << (segment - '0'))) ? WHITE : BLACK;
                            else
                                newc = BLACK;
                        }
                        else
                            newc = BLACK;
                    }
                    else
                        newc = BLACK;
                }
                break;
            case 4:
                // hlines
                if (!p)
                    newc = ((x / (8 - col / 14)) % 2) ? WHITE : BLACK;
                break;
            case 5:
                // vlines
                if (!p)
                    newc = ((y / (1 + col / 14)) % 2) ? WHITE : BLACK;
                break;
            case 6:
                // Y gradient
                if (p == PLANE_Y)
                    newc = (x - priv->box_x1) * 255 / (priv->box_x2 - priv->box_x1);
                break;
            case 7:
                // U gradient
                if (p == PLANE_U)
                    newc = (x - priv->box_x1) * 255 / (priv->box_x2 - priv->box_x1);
                break;
            case 8:
                // V gradient
                if (p == PLANE_V)
                    newc = (x - priv->box_x1) * 255 / (priv->box_x2 - priv->box_x1);
                break;
        }
    }

    // circle
    if(x >= priv->circle_x1 && x <= priv->circle_x2 && y >= priv->circle_y1 && y <= priv->circle_y2) {
        double circval_inside = ellipse_box_intersection(x-0.5, y-0.5, x+0.5, y+0.5, priv->circle_mx, priv->circle_my, priv->circle_rx2, priv->circle_ry2);
        double circval_outside = ellipse_box_intersection(x-0.5, y-0.5, x+0.5, y+0.5, priv->circle_mx, priv->circle_my, priv->circle_rx, priv->circle_ry);
        double a = circval_outside - circval_inside;
        if (p) {
            newc = a * GREY + (1 - a) * newc;
        } else {
            newc = a * WHITE + (1 - a) * newc;
        }
    }

    return newc;
}

static int put_image(struct vf_dlopen_context *ctx)
{
    privdata *priv = ctx->priv;
    unsigned p, y, x;
    struct vf_dlopen_picdata *in = &ctx->inpic;
    struct vf_dlopen_picdata *out = &ctx->outpic[0];

    // 15 seconds for a complete round
    int px = rint(priv->circle_mx + cos(in->pts * M_PI / 7.5) * (priv->circle_rx + priv->circle_rx2) / 2 - 0.5);
    int py = rint(priv->circle_my + sin(in->pts * M_PI / 7.5) * (priv->circle_ry + priv->circle_ry2) / 2 - 0.5);

    // build the pts string
    char ptsbuf[17];
    long milliseconds = lrint(in->pts * 1000);
    long sign = (milliseconds >= 0) ? +1 : -1;
    milliseconds *= sign;
    memset(ptsbuf, 0, sizeof(ptsbuf));
    snprintf(ptsbuf, sizeof(ptsbuf), "%c%05ld:%02ld:%02ld.%03ld",
            (sign >= 0) ? (int) ('0' + ((milliseconds % 3600000000000) / 360000000000)) : '-',
            (milliseconds % 360000000000) / 3600000,
            (milliseconds % 3600000) / 60000,
            (milliseconds % 60000) / 1000,
            milliseconds % 1000);

    for (p = 0; p < out->planes; ++p)
        for (y = 0; y < out->planeheight[p]; ++y)
        {
            unsigned char *pout = &out->plane[p][out->planestride[p] * y];
            unsigned char *pin = &in->plane[p][in->planestride[p] * y];
            for (x = 0; x < out->planewidth[p]; ++x)
                pout[x] = testimage_pixel(ctx, px, py, ptsbuf, p, x, y, pin[x]);
        }
    out->pts = in->pts;
    return 1;
}

int vf_dlopen_getcontext(struct vf_dlopen_context *ctx, int argc, const char **argv)
{
    VF_DLOPEN_CHECK_VERSION(ctx);
    (void) argc;
    (void) argv;
    static struct vf_dlopen_formatpair map[] = {
        { "444p", "444p" },
        { NULL, NULL }
    };
    ctx->format_mapping = map;
    ctx->put_image = put_image;
    ctx->config = config;
    ctx->priv = calloc(1, sizeof(privdata));
    return 1;
}
