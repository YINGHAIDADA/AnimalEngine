#pragma once

// OpenGL 4.1 Core function table, loaded through SDL on every platform.
// Only files under engine/src/rhi/opengl/ may include this header.

#include <GL/glcorearb.h>

#define ANIMAL_GL_FUNCTIONS(X)                                                      \
    X(PFNGLGETSTRINGPROC, glGetString)                                              \
    X(PFNGLGETINTEGERVPROC, glGetIntegerv)                                          \
    X(PFNGLGETERRORPROC, glGetError)                                                \
    X(PFNGLENABLEPROC, glEnable)                                                    \
    X(PFNGLDISABLEPROC, glDisable)                                                  \
    X(PFNGLENABLEIPROC, glEnablei)                                                  \
    X(PFNGLDISABLEIPROC, glDisablei)                                                \
    X(PFNGLVIEWPORTPROC, glViewport)                                                \
    X(PFNGLDEPTHRANGEFPROC, glDepthRangef)                                          \
    X(PFNGLSCISSORPROC, glScissor)                                                  \
    X(PFNGLCLEARCOLORPROC, glClearColor)                                            \
    X(PFNGLCLEARPROC, glClear)                                                      \
    X(PFNGLCOLORMASKPROC, glColorMask)                                              \
    X(PFNGLBLENDFUNCSEPARATEIPROC, glBlendFuncSeparatei)                            \
    X(PFNGLBLENDEQUATIONSEPARATEIPROC, glBlendEquationSeparatei)                    \
    X(PFNGLCULLFACEPROC, glCullFace)                                                \
    X(PFNGLFRONTFACEPROC, glFrontFace)                                              \
    X(PFNGLPIXELSTOREIPROC, glPixelStorei)                                          \
    X(PFNGLREADPIXELSPROC, glReadPixels)                                            \
    X(PFNGLFINISHPROC, glFinish)                                                    \
    X(PFNGLGENBUFFERSPROC, glGenBuffers)                                            \
    X(PFNGLDELETEBUFFERSPROC, glDeleteBuffers)                                      \
    X(PFNGLBINDBUFFERPROC, glBindBuffer)                                            \
    X(PFNGLBUFFERDATAPROC, glBufferData)                                            \
    X(PFNGLBUFFERSUBDATAPROC, glBufferSubData)                                      \
    X(PFNGLBINDBUFFERRANGEPROC, glBindBufferRange)                                  \
    X(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays)                                  \
    X(PFNGLDELETEVERTEXARRAYSPROC, glDeleteVertexArrays)                            \
    X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray)                                  \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray)                  \
    X(PFNGLDISABLEVERTEXATTRIBARRAYPROC, glDisableVertexAttribArray)                \
    X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer)                          \
    X(PFNGLVERTEXATTRIBDIVISORPROC, glVertexAttribDivisor)                          \
    X(PFNGLGENTEXTURESPROC, glGenTextures)                                          \
    X(PFNGLDELETETEXTURESPROC, glDeleteTextures)                                    \
    X(PFNGLBINDTEXTUREPROC, glBindTexture)                                          \
    X(PFNGLTEXIMAGE2DPROC, glTexImage2D)                                            \
    X(PFNGLTEXSUBIMAGE2DPROC, glTexSubImage2D)                                      \
    X(PFNGLTEXPARAMETERIPROC, glTexParameteri)                                      \
    X(PFNGLACTIVETEXTUREPROC, glActiveTexture)                                      \
    X(PFNGLGENSAMPLERSPROC, glGenSamplers)                                          \
    X(PFNGLDELETESAMPLERSPROC, glDeleteSamplers)                                    \
    X(PFNGLBINDSAMPLERPROC, glBindSampler)                                          \
    X(PFNGLSAMPLERPARAMETERIPROC, glSamplerParameteri)                              \
    X(PFNGLCREATESHADERPROC, glCreateShader)                                        \
    X(PFNGLDELETESHADERPROC, glDeleteShader)                                        \
    X(PFNGLSHADERSOURCEPROC, glShaderSource)                                        \
    X(PFNGLCOMPILESHADERPROC, glCompileShader)                                      \
    X(PFNGLGETSHADERIVPROC, glGetShaderiv)                                          \
    X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog)                                \
    X(PFNGLCREATEPROGRAMPROC, glCreateProgram)                                      \
    X(PFNGLDELETEPROGRAMPROC, glDeleteProgram)                                      \
    X(PFNGLATTACHSHADERPROC, glAttachShader)                                        \
    X(PFNGLDETACHSHADERPROC, glDetachShader)                                        \
    X(PFNGLLINKPROGRAMPROC, glLinkProgram)                                          \
    X(PFNGLGETPROGRAMIVPROC, glGetProgramiv)                                        \
    X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog)                              \
    X(PFNGLUSEPROGRAMPROC, glUseProgram)                                            \
    X(PFNGLGETUNIFORMBLOCKINDEXPROC, glGetUniformBlockIndex)                        \
    X(PFNGLUNIFORMBLOCKBINDINGPROC, glUniformBlockBinding)                          \
    X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation)                            \
    X(PFNGLUNIFORM1IPROC, glUniform1i)                                              \
    X(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers)                                  \
    X(PFNGLDELETEFRAMEBUFFERSPROC, glDeleteFramebuffers)                            \
    X(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer)                                  \
    X(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D)                        \
    X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus)                    \
    X(PFNGLBLITFRAMEBUFFERPROC, glBlitFramebuffer)                                  \
    X(PFNGLDRAWBUFFERSPROC, glDrawBuffers)                                          \
    X(PFNGLCLEARBUFFERFVPROC, glClearBufferfv)                                      \
    X(PFNGLDRAWARRAYSINSTANCEDPROC, glDrawArraysInstanced)                          \
    X(PFNGLDRAWELEMENTSINSTANCEDBASEVERTEXPROC, glDrawElementsInstancedBaseVertex)  \
    X(PFNGLGENQUERIESPROC, glGenQueries)                                            \
    X(PFNGLDELETEQUERIESPROC, glDeleteQueries)                                      \
    X(PFNGLQUERYCOUNTERPROC, glQueryCounter)                                        \
    X(PFNGLBEGINQUERYPROC, glBeginQuery)                                            \
    X(PFNGLENDQUERYPROC, glEndQuery)                                                \
    X(PFNGLGETQUERYIVPROC, glGetQueryiv)                                            \
    X(PFNGLGETQUERYOBJECTIVPROC, glGetQueryObjectiv)                                \
    X(PFNGLGETQUERYOBJECTUI64VPROC, glGetQueryObjectui64v)

namespace animal::rhi::gl {

#define ANIMAL_GL_DECLARE(type, name) extern type name;
ANIMAL_GL_FUNCTIONS(ANIMAL_GL_DECLARE)
#undef ANIMAL_GL_DECLARE

// Requires a current context. Logs every missing entry point.
bool loadFunctions();

} // namespace animal::rhi::gl
