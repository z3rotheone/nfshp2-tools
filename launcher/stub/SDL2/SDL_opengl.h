/* NOT OpenGL. A minimal stand-in for SDL_opengl.h declaring only what
 * track_main.c uses, for `make stubcheck`. Never linked; never on the
 * include path of a real build. Values are OpenGL's own. */
#ifndef HP2_GL_STUB_H
#define HP2_GL_STUB_H

typedef unsigned int   GLenum;
typedef unsigned char  GLboolean;
typedef unsigned int   GLbitfield;
typedef void           GLvoid;
typedef int            GLint;
typedef unsigned char  GLubyte;
typedef unsigned int   GLuint;
typedef int            GLsizei;
typedef float          GLfloat;
typedef float          GLclampf;

#define GL_ALPHA_SCALE             0x0D1C
#define GL_ALPHA_TEST              0x0BC0
#define GL_BLEND                   0x0BE2
#define GL_COLOR_BUFFER_BIT        0x00004000
#define GL_COMPILE                 0x1300
#define GL_CULL_FACE               0x0B44
#define GL_DEPTH_BUFFER_BIT        0x00000100
#define GL_DEPTH_TEST              0x0B71
#define GL_FALSE                   0
#define GL_FOG                     0x0B60
#define GL_FOG_COLOR               0x0B66
#define GL_FOG_END                 0x0B64
#define GL_FOG_MODE                0x0B65
#define GL_FOG_START               0x0B63
#define GL_GREATER                 0x0204
#define GL_LEQUAL                  0x0203
#define GL_LINEAR                  0x2601
#define GL_LINEAR_MIPMAP_LINEAR    0x2703
#define GL_MODELVIEW               0x1700
#define GL_MODULATE                0x2100
#define GL_ONE_MINUS_SRC_ALPHA     0x0303
#define GL_PACK_ALIGNMENT          0x0D05
#define GL_PROJECTION              0x1701
#define GL_REPEAT                  0x2901
#define GL_RGBA                    0x1908
#define GL_SRC_ALPHA               0x0302
#define GL_TEXTURE                 0x1702
#define GL_TEXTURE_2D              0x0DE1
#define GL_TEXTURE_ENV             0x2300
#define GL_TEXTURE_ENV_MODE        0x2200
#define GL_TEXTURE_MAG_FILTER      0x2800
#define GL_TEXTURE_MIN_FILTER      0x2801
#define GL_TEXTURE_WRAP_S          0x2802
#define GL_TEXTURE_WRAP_T          0x2803
#define GL_TRIANGLES               0x0004
#define GL_TRUE                    1
#define GL_UNSIGNED_BYTE           0x1401
#define GL_VERSION                 0x1F02

void glAlphaFunc(GLenum func, GLclampf ref);
void glBegin(GLenum mode);
void glBindTexture(GLenum target, GLuint texture);
void glBlendFunc(GLenum sfactor, GLenum dfactor);
void glCallList(GLuint list);
void glClear(GLbitfield mask);
void glClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha);
void glColor4ub(GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha);
void glDepthFunc(GLenum func);
void glDepthMask(GLboolean flag);
void glDisable(GLenum cap);
void glEnable(GLenum cap);
void glEnd(void);
void glEndList(void);
void glFinish(void);
void glFogf(GLenum pname, GLfloat param);
void glFogfv(GLenum pname, const GLfloat *params);
void glFogi(GLenum pname, GLint param);
GLuint glGenLists(GLsizei range);
void glGenTextures(GLsizei n, GLuint *textures);
const GLubyte * glGetString(GLenum name);
void glLoadMatrixf(const GLfloat *m);
void glMatrixMode(GLenum mode);
void glNewList(GLuint list, GLenum mode);
void glPixelStorei(GLenum pname, GLint param);
void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels);
void glTexCoord2f(GLfloat s, GLfloat t);
void glTexEnvf(GLenum target, GLenum pname, GLfloat param);
void glTexEnvi(GLenum target, GLenum pname, GLint param);
void glTexImage2D(GLenum target, GLint level, GLint internalFormat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels);
void glTexParameteri(GLenum target, GLenum pname, GLint param);
void glVertex3f(GLfloat x, GLfloat y, GLfloat z);
void glViewport(GLint x, GLint y, GLsizei width, GLsizei height);
#define GL_POLYGON_OFFSET_FILL     0x8037
#define GL_TRIANGLE_FAN            0x0006
void glPushMatrix(void);
void glPopMatrix(void);
void glTranslatef(GLfloat x, GLfloat y, GLfloat z);
void glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
void glScalef(GLfloat x, GLfloat y, GLfloat z);
void glPolygonOffset(GLfloat factor, GLfloat units);

#endif
