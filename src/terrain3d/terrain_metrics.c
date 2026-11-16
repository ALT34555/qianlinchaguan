#define QLCG_GL_METRICS_IMPL
#include "gl_loader.h"
#include "terrain_metrics.h"
#include "core/math_util.h"
TerrainMetrics qlcg_metrics,qlcg_metrics_last;
void qlcg_metric_arrays(GLenum mode,GLint first,GLsizei count) { qlcg_metrics.draws++;if(mode==GL_TRIANGLES)qlcg_metrics.triangles+=count/3;glDrawArrays(mode,first,count); }
void qlcg_metric_elements(GLenum mode,GLsizei count,GLenum type,const void *indices) { qlcg_metrics.draws++;if(mode==GL_TRIANGLES)qlcg_metrics.triangles+=count/3;glDrawElements(mode,count,type,indices); }
void qlcg_metric_arrays_inst(GLenum mode,GLint first,GLsizei count,GLsizei n) { qlcg_metrics.draws++;if(mode==GL_TRIANGLES)qlcg_metrics.triangles+=(uint64_t)(count/3)*n;glDrawArraysInstanced(mode,first,count,n); }
void qlcg_metric_elements_inst(GLenum mode,GLsizei count,GLenum type,const void *indices,GLsizei n) { qlcg_metrics.draws++;if(mode==GL_TRIANGLES)qlcg_metrics.triangles+=(uint64_t)(count/3)*n;glDrawElementsInstanced(mode,count,type,indices,n); }
void qlcg_metric_multi(GLenum mode,const GLsizei *counts,GLenum type,const void *const *indices,GLsizei n,const GLint *base) { qlcg_metrics.draws++;if(mode==GL_TRIANGLES)for(int i=0;i<n;i++)qlcg_metrics.triangles+=counts[i]/3;glMultiDrawElementsBaseVertex(mode,counts,type,indices,n,base); }
void qlcg_metric_data(GLenum target,GLsizeiptr size,const void *data,GLenum usage) { if(data)qlcg_metrics.upload_bytes+=size;double start=qlcg_time_ms();glBufferData(target,size,data,usage);qlcg_metrics.buffer_ms+=qlcg_time_ms()-start; }
void qlcg_metric_subdata(GLenum target,GLintptr offset,GLsizeiptr size,const void *data) { qlcg_metrics.upload_bytes+=size;double start=qlcg_time_ms();glBufferSubData(target,offset,size,data);qlcg_metrics.buffer_ms+=qlcg_time_ms()-start; }
