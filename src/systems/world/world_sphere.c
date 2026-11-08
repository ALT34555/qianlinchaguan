/* planet_sphere */
#include "planet_macro.h"
#include "world_internal.h"
#include "../../core/math_util.h"
#include "platform/platform.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

void rotate_vec3(const float v[3], const float k[3], float a, float out[3]) {
    float ca = cosf(a), sa = sinf(a);
    float d = qlcg_dot3(k, v);
    float cx[3];
    qlcg_cross3(k, v, cx);
    for (int i = 0; i < 3; ++i) {
        out[i] = v[i] * ca + cx[i] * sa + k[i] * d * (1.0f - ca);
    }
    qlcg_unit3(out);
}

/* 构建球面网格与八邻域弧长 */
void build_sphere_grid(MacroWorld *w) {
    const double pi = 3.14159265358979323846;
    int cols = w->columns, rows = w->rows;
    double radius = w->circumference / (2*pi);
    static const int dxs[8] = {-1,1,0,0,-1,1,-1,1};
    static const int dys[8] = {0,0,-1,1,-1,-1,1,1};
    for (int y = 0; y < rows; y++) {
        double lat = pi/2 - (y+.5)*pi/rows;
        double area = radius*radius*(2*pi/cols) *
            (sin(pi/2-y*pi/rows)-sin(pi/2-(y+1)*pi/rows))/1e6;
        for (int x = 0; x < cols; x++) {
            int i = y*cols+x;
            double lon = (x+.5)/cols*(2*pi)-pi;
            w->points[i*3] = (float)(cos(lat)*cos(lon));
            w->points[i*3+1] = (float)sin(lat);
            w->points[i*3+2] = (float)(cos(lat)*sin(lon));
            if(qlcg_world_flat(w)) {
                w->points[i*3]=(2.0f*x+1)/cols-1;
                w->points[i*3+1]=0;
                w->points[i*3+2]=(2.0f*y+1)/rows-1;
            }
            w->areas[i] = qlcg_world_flat(w) ? (w->circumference/cols)*(w->circumference/cols)/1e6f : (float)area;
        }
    }
    for (int y = 0; y < rows; y++) for (int x = 0; x < cols; x++) {
        int i = y*cols+x;
        for (int d = 0; d < 8; d++) {
            int ny = y+dys[d], nx = x+dxs[d];
            if (qlcg_world_flat(w)) {
                nx=qlcg_clampi(nx,0,cols-1); ny=qlcg_clampi(ny,0,rows-1);
                w->neighbors[i*8+d]=ny*cols+nx;
                w->lengths[i*8+d]=w->circumference/cols*(d<4?1.0f:1.41421356f);
                continue;
            }
            if (ny < 0) { ny = -ny-1; nx += cols/2; }
            if (ny >= rows) { ny = rows*2-ny-1; nx += cols/2; }
            int j = ny*cols+wrap_col(nx,cols);
            w->neighbors[i*8+d] = j;
            double dx = (double)w->points[i*3]-w->points[j*3];
            double dy = (double)w->points[i*3+1]-w->points[j*3+1];
            double dz = (double)w->points[i*3+2]-w->points[j*3+2];
            double chord = fmin(1, sqrt(dx*dx+dy*dy+dz*dz)/2);
            w->lengths[i*8+d] = (float)(2*asin(chord)*radius);
        }
    }
}

/* 构建主微构造板块与碰撞裂谷场 */
