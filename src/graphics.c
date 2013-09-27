/*! @file graphics.c
 *  @copyright Copyright (c) 2013 Kyle Weicht. All rights reserved.
 */
#include "graphics.h"
#include <stdlib.h>
#include <string.h>
#if defined(__APPLE__)
    #include <OpenGLES/ES3/gl.h>
    #include <OpenGLES/ES3/glext.h>
#elif defined(__ANDROID__)
    #include <GLES3/gl3.h>
#else
    #error Need an OpenGL implementation
#endif
#include "gl_helper.h"
#include "geometry.h"
#include "system.h"
#include "assert.h"
#include "vec_math.h"

/* Defines
 */
#define MAX_MESHES 32
#define MAX_TEXTURES 64
#define MAX_RENDER_COMMANDS 1024
#define MAX_LIGHTS 64

/* Types
 */
typedef struct RenderCommand
{
    Transform   transform;
    Mesh*       mesh;
    Texture*    diffuse;
} RenderCommand;

struct Texture
{
    GLuint  texture;
};

struct Graphics
{
    GLuint  program;
    GLuint  projection_uniform;
    GLuint  view_uniform;
    GLuint  world_uniform;
    GLuint  diffuse_uniform;
    GLuint  lights_uniform;
    GLuint  num_lights_uniform;

    GLuint  color_renderbuffer;
    GLuint  depth_renderbuffer;
    GLuint  depth_texture;
    GLuint  framebuffer;

    int width;
    int height;

    Mat4    projection_matrix;
    Transform   view_transform;

    GLuint  fullscreen_program;
    GLuint  fullscreen_texture_uniform;

    Mesh*  cube_mesh;
    Mesh*  quad_mesh;

    RenderCommand   commands[MAX_RENDER_COMMANDS];
    int num_commands;
    Light   lights[MAX_LIGHTS];
    int num_lights;
};


/* Constants
 */

/* Variables
 */

/* Internal functions
 */
static void _setup_framebuffer(Graphics* graphics)
{
    GLenum framebuffer_status;

    /* Color buffer */
    glGenTextures(1, &graphics->color_renderbuffer);
    glBindTexture(GL_TEXTURE_2D, graphics->color_renderbuffer);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, graphics->width, graphics->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
    CheckGLError();

    /* Depth buffer */
    glGenTextures(1, &graphics->depth_renderbuffer);
    glBindTexture(GL_TEXTURE_2D, graphics->depth_renderbuffer);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, graphics->width, graphics->height, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, 0);
    CheckGLError();

    /* Framebuffer */
    glGenFramebuffers(1, &graphics->framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, graphics->framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, graphics->color_renderbuffer, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, graphics->depth_renderbuffer, 0);

    framebuffer_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    switch (framebuffer_status) {
        case GL_FRAMEBUFFER_COMPLETE: break;
        case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
            system_log("Framebuffer Object %d Error: Attachment Point Unconnected", graphics->framebuffer);
            break;
        case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
            system_log("Framebuffer Object %d Error: Missing Attachment", graphics->framebuffer);
            break;
        case GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS:
            system_log("Framebuffer Object %d Error: Dimensions do not match", graphics->framebuffer);
            break;
        case GL_FRAMEBUFFER_UNSUPPORTED:
            system_log("Framebuffer Object %d Error: Unsupported Framebuffer Configuration", graphics->framebuffer);
            break;
        default:
            system_log("Framebuffer Object %d Error: Unkown Framebuffer Object Failure", graphics->framebuffer);
            break;
    }


    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    CheckGLError();

    system_log("Created framebuffer\n");
}
static GLuint _create_program(const char* vertex_shader_file, const char* fragment_shader_file,
                              const AttributeSlot* attribute_slots, int num_attributes )
{
    GLuint vertex_shader = gl_load_shader(vertex_shader_file, GL_VERTEX_SHADER);
    GLuint fragment_shader = gl_load_shader(fragment_shader_file, GL_FRAGMENT_SHADER);
    GLuint program;
    GLint  link_status;
    int ii;

    /* Create program */
    program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    for(ii=0;ii<num_attributes;++ii) {
        glBindAttribLocation(program, attribute_slots[ii], kAttributeSlotNames[attribute_slots[ii]]);
    }
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &link_status);
    if(link_status == GL_FALSE) {
        char message[1024];
        glGetProgramInfoLog(program, sizeof(message), 0, message);
        system_log(message);
        assert(link_status != GL_FALSE);
    }
    glDetachShader(program, fragment_shader);
    glDetachShader(program, vertex_shader);
    glDeleteShader(fragment_shader);
    glDeleteShader(vertex_shader);
    CheckGLError();

    return program;
}
static void _setup_programs(Graphics* graphics)
{
    { /* Create 3D program */
        AttributeSlot slots[] = {
            kPositionSlot,
            kNormalSlot,
            kTexCoordSlot
        };
        graphics->program = _create_program("SimpleVertex.glsl", "SimpleFragment.glsl", slots, 3);

        graphics->projection_uniform = glGetUniformLocation(graphics->program, "Projection");
        graphics->view_uniform = glGetUniformLocation(graphics->program, "View");
        graphics->world_uniform = glGetUniformLocation(graphics->program, "World");
        graphics->diffuse_uniform = glGetUniformLocation(graphics->program, "s_Diffuse");
        graphics->lights_uniform = glGetUniformLocation(graphics->program, "Lights[0].direction");
        graphics->num_lights_uniform = glGetUniformLocation(graphics->program, "NumLights");
        system_log("Created program\n");
    }

    { /* Fullscreen time */
        AttributeSlot slots[] = {
            kPositionSlot,
            kTexCoordSlot
        };
        graphics->fullscreen_program = _create_program("fullscreen_vertex.glsl", "fullscreen_fragment.glsl", slots, 2);

        graphics->fullscreen_texture_uniform = glGetUniformLocation(graphics->fullscreen_program, "s_Diffuse");
        system_log("Created fullscreen program\n");
    }
    CheckGLError();
}
static void _draw_mesh(const Mesh* mesh)
{
    const VertexDescription* desc = kVertexDescriptions[mesh->type];
    intptr_t ptr = 0;
    glBindBuffer(GL_ARRAY_BUFFER, mesh->vertex_buffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->index_buffer);
    do {
        glVertexAttribPointer(desc->slot, desc->count, GL_FLOAT, GL_FALSE, mesh->vertex_size, (void*)ptr);
        ptr += sizeof(float) * desc->count;
    } while((++desc)->count);
    glDrawElements(GL_TRIANGLES, mesh->index_count, mesh->index_format, NULL);
}

/* External functions
 */
Graphics* create_graphics(int width, int height)
{
    Graphics* graphics = NULL;

    /* Allocate device */
    graphics = (Graphics*)calloc(1, sizeof(*graphics));
    graphics->width = width;
    graphics->height = height;
    system_log("Graphics created. W: %d  H: %d\n", width, height);

    /* Perform GL initialization */
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glFrontFace(GL_CW);
    glViewport(0, 0, width, height);
    glClearColor(0.0f, 0.2f, 0.4f, 1.0f);
    glClearDepthf(1.0f);
    system_log("OpenGL version:\t%s\n", glGetString(GL_VERSION));
    system_log("OpenGL renderer:\t%s\n", glGetString(GL_RENDERER));

    /* Perform other initialization */
    _setup_framebuffer(graphics);
    _setup_programs(graphics);

    graphics->projection_matrix = mat4_perspective_fov(kPiDiv2,
                                                       width/(float)height,
                                                       0.1f,
                                                       1000.0f);
    graphics->view_transform = transform_zero;

    graphics->cube_mesh = gl_create_mesh(kCubeVertices, sizeof(kCubeVertices),
                                         kCubeIndices, sizeof(kCubeIndices),
                                         sizeof(kCubeIndices)/sizeof(kCubeIndices[0]),
                                         sizeof(kCubeVertices[0]), kPosNormTexVertex);

    graphics->quad_mesh  = gl_create_mesh(kQuadVertices, sizeof(kQuadVertices),
                                          kQuadIndices, sizeof(kQuadIndices),
                                          sizeof(kQuadIndices)/sizeof(kQuadIndices[0]),
                                          sizeof(kQuadVertices[0]), kPosNormTexVertex);

    CheckGLError();
    system_log("Graphics initialized\n");

    { /* Print extensions */
        char buffer[1024*16] = {0};
        uint32_t ii;
        strcpy(buffer,(const char*)glGetString(GL_EXTENSIONS));
        for(ii=0;ii<strlen(buffer);++ii) {
            if(buffer[ii] == ' ')
                buffer[ii] = '\n';
        }
        system_log("%s\n", buffer);
    }
    return graphics;
}
void render_graphics(Graphics* graphics)
{
    Mat4 view_matrix = mat4_inverse(transform_get_matrix(graphics->view_transform));
    int ii;

    GLint defaultFBO;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &defaultFBO);

    /** Bind framebuffer 
     */
    glBindFramebuffer(GL_FRAMEBUFFER, graphics->framebuffer);
    glClearColor(0.0f, 0.2f, 0.4f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    CheckGLError();

    glUseProgram(graphics->program);
    glEnableVertexAttribArray(kPositionSlot);
    glEnableVertexAttribArray(kNormalSlot);
    glEnableVertexAttribArray(kTexCoordSlot);
    CheckGLError();
    glUniformMatrix4fv(graphics->projection_uniform, 1, GL_FALSE, (float*)&graphics->projection_matrix);
    glUniformMatrix4fv(graphics->view_uniform, 1, GL_FALSE, (float*)&view_matrix);
    /* Upload lights */
    glUniform3fv(graphics->lights_uniform, graphics->num_lights, (float*)graphics->lights);
    glUniform1i(graphics->num_lights_uniform, graphics->num_lights);

    glActiveTexture(GL_TEXTURE0);
    glUniform1i(graphics->diffuse_uniform, 0);

    /* Loop through render commands */
    for(ii=0;ii<graphics->num_commands;++ii) {
        RenderCommand command = graphics->commands[ii];
        Mat4 model = transform_get_matrix(command.transform);
        glUniformMatrix4fv(graphics->world_uniform, 1, GL_FALSE, (float*)&model);
        glBindTexture(GL_TEXTURE_2D, command.diffuse->texture);
        _draw_mesh(command.mesh);
    }

    graphics->num_commands = 0;
    graphics->num_lights = 0;

    CheckGLError();

    /** Back to default 
     */
    glBindFramebuffer(GL_FRAMEBUFFER, defaultFBO);

    glClearColor(1.0f, 0.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(graphics->fullscreen_program);
    glEnableVertexAttribArray(kPositionSlot );
    glEnableVertexAttribArray(kTexCoordSlot);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, graphics->color_renderbuffer);
    glUniform1i(graphics->fullscreen_texture_uniform, 0);
    CheckGLError();

    _draw_mesh(graphics->quad_mesh);

    glBindTexture(GL_TEXTURE_2D, 0);

    CheckGLError();
}
void destroy_graphics(Graphics* graphics)
{
    free(graphics);
}
Mesh* cube_mesh(Graphics* graphics)
{
    return graphics->cube_mesh;
}
Mesh* quad_mesh(Graphics* graphics)
{
    return graphics->quad_mesh;
}
void add_render_command(Graphics* graphics, Mesh* mesh, Texture* diffuse, Transform transform)
{
    int index = graphics->num_commands++;
    assert(index < MAX_RENDER_COMMANDS);
    graphics->commands[index].mesh = mesh;
    graphics->commands[index].transform = transform;
    graphics->commands[index].diffuse = diffuse;
}
void add_directional_light(Graphics* graphics, Light light)
{
    int index = graphics->num_lights++;
    assert(index < MAX_LIGHTS);
    graphics->lights[index] = light;
}
Texture* load_texture(Graphics* graphics, const char* filename)
{
    Texture* texture = (Texture*)calloc(1, sizeof(*texture));
    texture->texture = gl_load_texture(filename);
    return texture;
    (void)sizeof(graphics);
}
void set_view_transform(Graphics* graphics, Transform view)
{
    graphics->view_transform = view;
}
Mesh* create_mesh(Graphics* graphics, const char* filename)
{
    return gl_load_mesh(filename);
    (void)sizeof(graphics);
}
void destroy_mesh(Mesh* mesh)
{
    glDeleteBuffers(1, &mesh->vertex_buffer);
    glDeleteBuffers(1, &mesh->index_buffer);
    free(mesh);
}
void destroy_texture(Texture* texture)
{
    glDeleteTextures(1, &texture->texture);
    free(texture);
}


