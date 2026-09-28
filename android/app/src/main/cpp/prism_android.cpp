// PRISM ENGINE Android host: JNI + GLES 3 renderer + 2D physics + PrismScript.
// Renderer is self-contained in the APK. No downloads, network calls, or telemetry.
#include <jni.h>
#include <GLES3/gl3.h>
#include <android/log.h>
#include <cmath>
#include <algorithm>
#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include "prism/core/engine.h"
#include "prism/physics2d/physics2d.h"
#include "prism/script/prismscript.h"
#include "prism/math/math.h"

namespace {
using namespace prism;
using namespace prism::math;
using namespace prism::physics2d;
constexpr const char* TAG = "PRISM";
void log(const char* msg) { __android_log_print(ANDROID_LOG_INFO, TAG, "%s", msg); }

struct Renderer {
    GLuint program = 0, buffer = 0;
    GLint mvp = -1;
    int width = 1, height = 1;
    f32 elapsed = 0;
    f32 cube_angle = 0;

    static GLuint shader(GLenum type, const char* src) {
        GLuint s = glCreateShader(type);
        glShaderSource(s,1,&src,nullptr);
        glCompileShader(s);
        GLint ok = 0;
        glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
        if(!ok){char msg[1024]{};glGetShaderInfoLog(s,sizeof(msg),nullptr,msg);log(msg);}
        return s;
    }
    void create() {
        const char* vs = R"(#version 300 es
            layout(location=0) in vec3 aPos;
            layout(location=1) in vec3 aColor;
            uniform mat4 uMVP;
            out vec3 vColor;
            void main(){gl_Position=uMVP*vec4(aPos,1.0);vColor=aColor;}
        )";
        const char* fs = R"(#version 300 es
            precision mediump float;
            in vec3 vColor;
            out vec4 fragColor;
            void main(){
                vec3 c = vColor/(vColor+vec3(1.0));
                c = pow(c,vec3(1.0/2.2));
                fragColor=vec4(c,1.0);
            }
        )";
        GLuint v=shader(GL_VERTEX_SHADER,vs),f=shader(GL_FRAGMENT_SHADER,fs);
        program=glCreateProgram();
        glAttachShader(program,v);glAttachShader(program,f);glLinkProgram(program);
        GLint ok=0;glGetProgramiv(program,GL_LINK_STATUS,&ok);
        if(!ok){char msg[1024]{};glGetProgramInfoLog(program,sizeof(msg),nullptr,msg);log(msg);}
        glDeleteShader(v);glDeleteShader(f);
        mvp=glGetUniformLocation(program,"uMVP");
        glGenBuffers(1,&buffer);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glClearColor(0.039f,0.039f,0.071f,1.0f);
    }
    void destroy() {
        if(buffer)glDeleteBuffers(1,&buffer);
        if(program)glDeleteProgram(program);
        program=buffer=0;
    }
    void resize(int w,int h){width=std::max(1,w);height=std::max(1,h);glViewport(0,0,width,height);}
    void draw(const float* data,std::size_t floats,const Mat4& matrix) {
        glUseProgram(program);
        glUniformMatrix4fv(mvp,1,GL_FALSE,matrix.data());
        glBindBuffer(GL_ARRAY_BUFFER,buffer);
        glBufferData(GL_ARRAY_BUFFER,static_cast<GLsizeiptr>(floats*sizeof(float)),data,GL_STREAM_DRAW);
        glEnableVertexAttribArray(0);glEnableVertexAttribArray(1);
        glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,6*sizeof(float),nullptr);
        glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,6*sizeof(float),reinterpret_cast<void*>(3*sizeof(float)));
        glDrawArrays(GL_TRIANGLES,0,static_cast<GLsizei>(floats/6));
    }
    void rect(float x,float y,float w,float h,Vec3 color,const Mat4& vp) {
        const float r=color.x,g=color.y,b=color.z;
        float v[]={x,y,0,r,g,b, x+w,y,0,r,g,b, x+w,y+h,0,r,g,b,
                   x,y,0,r,g,b, x+w,y+h,0,r,g,b, x,y+h,0,r,g,b};
        draw(v,36,vp);
    }
    void cube(Vec3 center,float size,const Mat4& vp){
        constexpr float corners[8][3]={{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},
                                        {-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}};
        constexpr int faces[6][4]={{4,5,6,7},{1,0,3,2},{0,4,7,3},
                                   {5,1,2,6},{3,7,6,2},{0,1,5,4}};
        constexpr float colors[6][3]={{0.7f,0.16f,1.6f},{0.1f,0.7f,1.5f},
                                       {0.1f,1.6f,2.0f},{2.0f,0.4f,1.2f},
                                       {2.5f,1.5f,0.2f},{0.4f,0.2f,1.6f}};
        float data[6*6*6];int cursor=0;
        const int order[]={0,1,2,0,2,3};
        for(int face=0;face<6;++face)for(int i=0;i<6;++i){
            int vert=faces[face][order[i]];
            for(int j=0;j<3;++j)data[cursor++]=corners[vert][j]*size*0.5f;
            for(int j=0;j<3;++j)data[cursor++]=colors[face][j];
        }
        Mat4 model=Mat4::translate(center)*Mat4::rotate_y(cube_angle)*Mat4::rotate_x(cube_angle*0.43f);
        draw(data,static_cast<std::size_t>(cursor),vp*model);
    }
    void render(Vec2 player,float left,float right,bool jump){
        elapsed+=1.0f/60.0f;
        cube_angle+=0.013f;
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        float aspect=static_cast<float>(width)/static_cast<float>(height);
        // 3D scene on the right: prism cube lit by per-face spectrum colors.
        glEnable(GL_DEPTH_TEST);glEnable(GL_CULL_FACE);
        Mat4 p=Mat4::perspective(50*Deg2Rad,aspect,0.1f,100.f);
        Mat4 view=Mat4::look_at({0,1.4f,13},{0,0,0},{0,1,0});
        cube({3.1f,0.6f,0},2.5f,p*view);
        cube({5.8f,-1.0f,-2},0.6f,p*view);
        // 2D pixel-friendly layer on the left (same shader/buffer, orthographic camera).
        glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);
        Mat4 ortho=Mat4::ortho(-8*aspect/1.7778f,8*aspect/1.7778f,-4.5f,4.5f,-1,1);
        rect(-8,-4.3f,16,0.32f,{0.20f,0.12f,0.43f},ortho);
        rect(-8,-3.75f,7.8f,0.75f,{0.31f,0.12f,0.65f},ortho); // ground top y=-3
        rect(-5,-1.9f,1.7f,0.22f,{0.02f,0.8f,1.8f},ortho);  // floating platform
        rect(-2.6f,-0.7f,1.4f,0.20f,{2.5f,1.55f,0.1f},ortho);
        rect(player.x-0.35f,player.y-0.35f,0.7f,0.7f,{2.0f,0.15f,0.85f},ortho);
        // Ray mascot: spectrum beam + 3 colored accents at x=-7
        rect(-7.7f,1.3f,2.5f,0.07f,{2.0f,2.0f,2.0f},ortho);
        rect(-5.2f,1.32f,1.2f,0.08f,{2.0f,0.1f,0.5f},ortho);
        rect(-5.2f,1.15f,1.2f,0.08f,{0.2f,0.7f,2.0f},ortho);
        rect(-5.2f,0.98f,1.2f,0.08f,{2.2f,1.4f,0.15f},ortho);
        // touch pads
        rect(-7.7f,-4.4f,1.5f,0.42f,left>0?Vec3{0.1f,0.8f,2.0f}:Vec3{0.09f,0.18f,0.4f},ortho);
        rect(-5.8f,-4.4f,1.5f,0.42f,right>0?Vec3{0.1f,0.8f,2.0f}:Vec3{0.09f,0.18f,0.4f},ortho);
        rect(5.2f,-4.4f,2.0f,0.42f,jump?Vec3{2.5f,0.3f,1.2f}:Vec3{0.4f,0.13f,0.4f},ortho);
    }
};

std::unique_ptr<Engine> engine;
std::unique_ptr<World> physics;
std::unique_ptr<script::Interpreter> vm;
Renderer renderer;
BodyId player_id=kNullBody;
float left=0,right=0;
bool jump_pending=false,jump_down=false;
std::atomic<int> fps{0};
std::atomic<int> frames{0};

void init_scene(){
    PhysicsConfig cfg; cfg.fixed_dt=1.0f/60.0f;cfg.gravity={0,-13};
    physics=std::make_unique<World>(cfg);
    Body ground;ground.type=BodyType::Static;ground.position={-4.1f,-3.35f};
    Shape gs;gs.type=ShapeType::Box;gs.half_extents={3.9f,0.35f};ground.shapes.push_back(gs);
    physics->create_body(ground);
    Body platform;platform.type=BodyType::Static;platform.position={-4.15f,-1.79f};
    Shape ps;ps.half_extents={0.85f,0.11f};platform.shapes.push_back(ps);
    physics->create_body(platform);
    Body player;player.type=BodyType::Dynamic;player.position={-5.5f,-0.3f};
    player.fixed_rotation=true;player.linear_damping=1.2f;
    Shape shape;shape.half_extents={0.35f,0.35f};shape.friction=0.5f;
    player.shapes.push_back(shape);
    player_id=physics->create_body(player);
}
}

extern "C" {
JNIEXPORT void JNICALL Java_dev_prismengine_runtime_PrismBridge_nativeCreate(JNIEnv* env,jclass,jstring source){
    const char* utf=env->GetStringUTFChars(source,nullptr);
    engine=std::make_unique<Engine>(EngineConfig{"Prism 2D+3D Demo"});
    engine->boot();engine->start();
    vm=std::make_unique<script::Interpreter>();
    vm->run_source(utf,"sample.prism");
    env->ReleaseStringUTFChars(source,utf);
    init_scene();
    log("native runtime created, offline PrismScript loaded");
}
JNIEXPORT void JNICALL Java_dev_prismengine_runtime_PrismBridge_nativeSurfaceCreated(JNIEnv*,jclass){
    // EGL context can be recreated after pause; old handles belong to the lost context.
    renderer.program=renderer.buffer=0;
    renderer.create();
    const char* glr=reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const char* glv=reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    if(engine)engine->set_gpu_profile(glr?glr:"unknown",glv?glv:"unknown");
}
JNIEXPORT void JNICALL Java_dev_prismengine_runtime_PrismBridge_nativeResize(JNIEnv*,jclass,jint w,jint h){renderer.resize(w,h);}
JNIEXPORT void JNICALL Java_dev_prismengine_runtime_PrismBridge_nativeFrame(JNIEnv*,jclass,jfloat dt){
    if(!physics)return;
    if(engine)engine->frame();
    Body* p=physics->body(player_id);
    if(p){
        p->linear_velocity.x=(right-left)*4.3f;
        if(jump_pending){
            bool grounded=false;
            for(auto& c:physics->contacts())
                if(c.a==player_id||c.b==player_id){
                    if(!c.sensor&&c.normal.y*(c.a==player_id?1:-1)<-0.3f)grounded=true;
                }
            if(grounded)p->linear_velocity.y=6.8f;
            jump_pending=false;
        }
    }
    physics->step(std::min(dt,0.05f));
    p=physics->body(player_id);
    if(p&&p->position.y < -5){p->position={-5.5f,-0.3f};p->linear_velocity={0,0};}
    static float fps_acc=0;static int fps_count=0;
    fps_acc+=dt;++fps_count;
    if(fps_acc>=0.5f){fps.store(static_cast<int>(fps_count/fps_acc));fps_acc=0;fps_count=0;}
    frames.fetch_add(1);
    if(renderer.program)renderer.render(p?p->position:Vec2{-5,0},left,right,jump_down);
}
JNIEXPORT void JNICALL Java_dev_prismengine_runtime_PrismBridge_nativeTouch(JNIEnv*,jclass,jint action,jfloat x,jfloat y){
    if(y<0.55f)return;
    float l=x<0.27f?1.f:0.f,r=x>=0.27f&&x<0.53f?1.f:0.f;
    bool j=x>0.68f;
    if(action==0||action==1){left=l;right=r;if(j&&!jump_down)jump_pending=true;jump_down=j;}
    else{left=right=0;jump_down=false;}
    if(engine){if(action==0)engine->bus().publish(TouchBegan{0,x,y,1});
               if(action==1)engine->bus().publish(TouchMoved{0,x,y});
               if(action==2)engine->bus().publish(TouchEnded{0,x,y});}
}
JNIEXPORT jstring JNICALL Java_dev_prismengine_runtime_PrismBridge_nativeStats(JNIEnv* env,jclass){
    std::string msg="GLES 3  |  "+std::to_string(fps.load())+" FPS  |  "+std::to_string(frames.load())+" frames";
    return env->NewStringUTF(msg.c_str());
}
JNIEXPORT void JNICALL Java_dev_prismengine_runtime_PrismBridge_nativeDestroy(JNIEnv*,jclass){
    renderer.destroy();physics.reset();vm.reset();
    if(engine){engine->shutdown();engine.reset();}
}
}
