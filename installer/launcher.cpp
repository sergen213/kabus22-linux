#include "launcher_settings.hpp"
#include "layout.hpp"
#include "wizard_style.hpp"
#include "cgltf.h"
#include <epoxy/gl.h>
#include <gtk/gtk.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <cerrno>
#include <cmath>
#include <csignal>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace {
using namespace k22::launcher;
constexpr const char* Version = "kabus22 native launcher " K22_VERSION;
struct Options { fs::path root, engine; std::string renderer; std::optional<unsigned long long> frames; bool help=false, version=false, check=false; };
Options arguments(int argc,char** argv) {
    Options o;
    for(int i=1;i<argc;++i) {
        std::string flag=argv[i];
        if(flag=="--help"||flag=="-h") {o.help=true;continue;}
        if(flag=="--version") {o.version=true;continue;}
        if(flag=="--self-check") {o.check=true;continue;}
        if(i+1>=argc) throw std::runtime_error("Missing value for "+flag);
        std::string value=argv[++i]; if(value.empty())throw std::runtime_error("Empty value for "+flag);
        if(flag=="--install-dir") {if(!o.root.empty())throw std::runtime_error("Duplicate --install-dir");o.root=fs::absolute(value).lexically_normal();}
        else if(flag=="--engine") {if(!o.engine.empty())throw std::runtime_error("Duplicate --engine");o.engine=fs::absolute(value).lexically_normal();}
        else if(flag=="--renderer") {if(!o.renderer.empty())throw std::runtime_error("Duplicate --renderer");o.renderer=value;Preferences p;p.renderer=value;validatePreferences(p);}
        else if(flag=="--frames") {unsigned long long n=0;auto [end,ec]=std::from_chars(value.data(),value.data()+value.size(),n);if(ec!=std::errc{}||end!=value.data()+value.size()||o.frames)throw std::runtime_error("--frames requires one unsigned integer");o.frames=n;}
        else throw std::runtime_error("Unknown option: "+flag);
    }
    return o;
}
void help() {std::cout<<Version<<"\nUsage: kabus22-launcher [--install-dir ROOT] [--engine EXACT_ELF] [--renderer opengl|vulkan-raster|vulkan-rt] [--frames N] [--help] [--version] [--self-check]\nOpens settings for an existing native installation and runs the sibling kabus22-setup / kabus22-updater tools. --renderer selects the UI choice only. Apply saves; Play saves then launches the native engine, logging to $XDG_STATE_HOME/kabus22/logs. --frames bounds that child for diagnostics.\n";}
GtkWidget* combo(std::initializer_list<std::pair<const char*,const char*>> entries) {
    auto* w=gtk_combo_box_text_new();for(const auto& [id,label]:entries)gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(w),id,label);return w;
}
std::string selected(GtkWidget* widget) {const char* id=gtk_combo_box_get_active_id(GTK_COMBO_BOX(widget));if(!id)throw std::runtime_error("A settings selection is missing");return id;}
void select(GtkWidget* widget,const std::string& value) {gtk_combo_box_set_active_id(GTK_COMBO_BOX(widget),value.c_str());}
GtkWidget* note(const char* text) {auto* w=gtk_label_new(text);gtk_label_set_xalign(GTK_LABEL(w),0);gtk_label_set_line_wrap(GTK_LABEL(w),true);gtk_widget_set_margin_top(w,8);gtk_widget_set_margin_bottom(w,8);return w;}
GtkWidget* page(GtkWidget* book,const char* name,GtkWidget*& grid) {
    auto* scroll=gtk_scrolled_window_new(nullptr,nullptr);gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),GTK_POLICY_NEVER,GTK_POLICY_AUTOMATIC);
    grid=gtk_grid_new();gtk_grid_set_row_spacing(GTK_GRID(grid),9);gtk_grid_set_column_spacing(GTK_GRID(grid),18);gtk_container_set_border_width(GTK_CONTAINER(grid),18);
    gtk_container_add(GTK_CONTAINER(scroll),grid);gtk_notebook_append_page(GTK_NOTEBOOK(book),scroll,gtk_label_new(name));return scroll;
}
void row(GtkWidget* grid,int y,const char* label,GtkWidget* control,const char* tip=nullptr) {
    auto* l=gtk_label_new_with_mnemonic(label);gtk_label_set_xalign(GTK_LABEL(l),0);gtk_label_set_mnemonic_widget(GTK_LABEL(l),control);
    gtk_widget_set_hexpand(control,true);gtk_grid_attach(GTK_GRID(grid),l,0,y,1,1);gtk_grid_attach(GTK_GRID(grid),control,1,y,1,1);
    if(tip)gtk_widget_set_tooltip_text(control,tip);
}
std::string valid(const std::string& text) {gchar* v=g_utf8_make_valid(text.data(),static_cast<gssize>(text.size()));std::string r=v;g_free(v);return r;}
void setText(GtkWidget* view,const std::string& text) {gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)),valid(text).c_str(),-1);}
GtkWidget* textView(GtkWidget*& view,int height) {
    auto* scroll=gtk_scrolled_window_new(nullptr,nullptr);gtk_widget_set_size_request(scroll,-1,height);gtk_widget_set_hexpand(scroll,true);
    view=gtk_text_view_new();gtk_text_view_set_editable(GTK_TEXT_VIEW(view),false);gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view),false);gtk_text_view_set_monospace(GTK_TEXT_VIEW(view),true);gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view),GTK_WRAP_WORD_CHAR);
    gtk_container_add(GTK_CONTAINER(scroll),view);return scroll;
}
template<class F> GtkWidget* button(const char* label,gpointer data,F) {
    auto* b=gtk_button_new_with_mnemonic(label);
    g_signal_connect(b,"clicked",G_CALLBACK(+[](GtkButton*,gpointer d){F{}(d);}),data);return b;
}
fs::path tools() {return fs::read_symlink("/proc/self/exe").parent_path();}
constexpr const char* padLabels[]={"A","B","X","Y","Left shoulder","Right shoulder","Left trigger","Right trigger","Back / View","Start / Menu","Guide","Left stick press","Right stick press","D-pad up","D-pad down","D-pad left","D-pad right","Misc / Share","Paddle 1","Paddle 2","Paddle 3","Paddle 4","Touchpad"};
// Mods page 3-D viewer: one glTF (the engine's --export-model output) with node transforms, base colour texture × factor,
// smooth normals and a head light. Frame k = rest + morph target k, blended on the CPU. ponytail: BLEND materials draw opaque.
struct Viewer {
    struct Part {std::vector<float> rest,uv;std::vector<std::vector<float>> deltas;std::vector<unsigned> index;GdkPixbuf* image=nullptr;std::array<float,4> color{1,1,1,1};float cutoff=-1;GLuint vao=0,vbo=0,ebo=0,tex=0;};
    GtkWidget* area=nullptr;std::vector<Part> parts;std::vector<std::string> frames;std::size_t vertices=0,triangles=0;std::array<float,3> center{};float radius=1;
    GLuint program=0,white=0;int frame=-1;bool upload=false,dirty=false,drag=false;double yaw=0.6,pitch=0.25,zoom=1.25,x=0,y=0;
    void reset(){yaw=0.6;pitch=0.25;zoom=1.25;}
    void release(){for(auto& p:parts){glDeleteVertexArrays(1,&p.vao);glDeleteBuffers(1,&p.vbo);glDeleteBuffers(1,&p.ebo);glDeleteTextures(1,&p.tex);p.vao=p.vbo=p.ebo=p.tex=0;}upload=true;}
    void clear(){
        if(area&&gtk_widget_get_realized(area)){gtk_gl_area_make_current(GTK_GL_AREA(area));if(!gtk_gl_area_get_error(GTK_GL_AREA(area)))release();}
        for(auto& p:parts){if(p.image)g_object_unref(p.image);}
        parts.clear();frames.clear();vertices=triangles=0;frame=-1;
    }
    static GdkPixbuf* image(const cgltf_image& image,const fs::path& file) {
        GdkPixbuf* loaded=nullptr;
        if(image.uri&&!std::string_view(image.uri).starts_with("data:")){std::string uri=image.uri;uri.resize(cgltf_decode_uri(uri.data()));loaded=gdk_pixbuf_new_from_file((file.parent_path()/uri).c_str(),nullptr);}
        else if(image.buffer_view&&image.buffer_view->buffer->data){
            auto* loader=gdk_pixbuf_loader_new();const auto* bytes=static_cast<const guchar*>(image.buffer_view->buffer->data)+image.buffer_view->offset;
            if(gdk_pixbuf_loader_write(loader,bytes,image.buffer_view->size,nullptr)&&gdk_pixbuf_loader_close(loader,nullptr)&&gdk_pixbuf_loader_get_pixbuf(loader))loaded=GDK_PIXBUF(g_object_ref(gdk_pixbuf_loader_get_pixbuf(loader)));
            else gdk_pixbuf_loader_close(loader,nullptr);
            g_object_unref(loader);
        }
        if(!loaded)return nullptr;
        auto* rgba=gdk_pixbuf_add_alpha(loaded,false,0,0,0);g_object_unref(loaded);return rgba;
    }
    void load(const fs::path& file) {
        cgltf_options options{};cgltf_data* data=nullptr;
        if(cgltf_parse_file(&options,file.c_str(),&data)!=cgltf_result_success)throw std::runtime_error("Cannot read "+file.string());
        std::unique_ptr<cgltf_data,decltype(&cgltf_free)> owned(data,cgltf_free);
        if(cgltf_load_buffers(&options,data,file.c_str())!=cgltf_result_success||cgltf_validate(data)!=cgltf_result_success)throw std::runtime_error("Cannot load the buffers of "+file.string());
        std::vector<std::pair<const cgltf_mesh*,std::array<float,16>>> meshes;
        for(std::size_t n=0;n<data->nodes_count;++n)if(data->nodes[n].mesh){std::array<float,16> m;cgltf_node_transform_world(&data->nodes[n],m.data());meshes.emplace_back(data->nodes[n].mesh,m);}
        if(meshes.empty())for(std::size_t m=0;m<data->meshes_count;++m)meshes.emplace_back(&data->meshes[m],std::array<float,16>{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1});
        clear();float lo[3],hi[3];for(int a=0;a<3;++a){lo[a]=std::numeric_limits<float>::max();hi[a]=-lo[a];}
        auto unpack=[](const cgltf_accessor* a,std::size_t width){std::vector<float> v(a->count*width);cgltf_accessor_unpack_floats(a,v.data(),v.size());return v;};
        auto find=[](const cgltf_attribute* list,std::size_t count,cgltf_attribute_type type,int index)->const cgltf_accessor*{for(std::size_t a=0;a<count;++a)if(list[a].type==type&&list[a].index==index)return list[a].data;return nullptr;};
        for(const auto& [mesh,m]:meshes){
            if(frames.empty())for(std::size_t t=0;t<mesh->target_names_count;++t)frames.emplace_back(mesh->target_names[t]);
            for(std::size_t i=0;i<mesh->primitives_count;++i){
                const auto& prim=mesh->primitives[i];const auto* position=find(prim.attributes,prim.attributes_count,cgltf_attribute_type_position,0);
                if(prim.type!=cgltf_primitive_type_triangles||!position||!position->count)continue;
                Part part;const auto count=position->count;part.rest=unpack(position,3);
                for(std::size_t v=0;v<count;++v){float* p=&part.rest[v*3];float q[3];for(int a=0;a<3;++a){q[a]=m[a]*p[0]+m[4+a]*p[1]+m[8+a]*p[2]+m[12+a];lo[a]=std::min(lo[a],q[a]);hi[a]=std::max(hi[a],q[a]);}std::copy(q,q+3,p);}
                const auto* uv=find(prim.attributes,prim.attributes_count,cgltf_attribute_type_texcoord,0);part.uv=uv&&uv->count==count?unpack(uv,2):std::vector<float>(count*2,0.f);
                for(std::size_t t=0;t<prim.targets_count;++t){
                    const auto* d=find(prim.targets[t].attributes,prim.targets[t].attributes_count,cgltf_attribute_type_position,0);auto delta=d&&d->count==count?unpack(d,3):std::vector<float>(count*3,0.f);
                    for(std::size_t v=0;v<count;++v){float* p=&delta[v*3];float q[3];for(int a=0;a<3;++a)q[a]=m[a]*p[0]+m[4+a]*p[1]+m[8+a]*p[2];std::copy(q,q+3,p);}
                    part.deltas.push_back(std::move(delta));
                }
                if(prim.indices)for(std::size_t k=0;k+2<prim.indices->count;++k)part.index.push_back(static_cast<unsigned>(cgltf_accessor_read_index(prim.indices,k)));
                else for(unsigned k=0;k<count;++k)part.index.push_back(k);
                part.index.resize(part.index.size()/3*3);if(std::any_of(part.index.begin(),part.index.end(),[&](unsigned k){return k>=count;}))continue;
                if(const auto* material=prim.material){
                    if(material->has_pbr_metallic_roughness){const auto& pbr=material->pbr_metallic_roughness;std::copy(pbr.base_color_factor,pbr.base_color_factor+4,part.color.begin());
                        if(pbr.base_color_texture.texture&&pbr.base_color_texture.texture->image)part.image=image(*pbr.base_color_texture.texture->image,file);}
                    if(material->alpha_mode==cgltf_alpha_mode_mask)part.cutoff=material->alpha_cutoff;
                }
                vertices+=count;triangles+=part.index.size()/3;parts.push_back(std::move(part));
            }
        }
        if(parts.empty())throw std::runtime_error(file.string()+" has no triangle mesh");
        std::size_t targets=parts[0].deltas.size();for(const auto& p:parts)targets=std::min(targets,p.deltas.size());
        frames.resize(targets);for(std::size_t t=0;t<targets;++t)if(frames[t].empty())frames[t]="frame "+std::to_string(t);
        for(int a=0;a<3;++a){center[a]=(lo[a]+hi[a])/2;}
        radius=std::max(1e-3f,std::hypot(hi[0]-lo[0],hi[1]-lo[1],hi[2]-lo[2])/2);
        frame=targets?0:-1;upload=dirty=true;
    }
    void realize() {
        auto shader=[](GLenum kind,const char* text){auto s=glCreateShader(kind);glShaderSource(s,1,&text,nullptr);glCompileShader(s);return s;};
        auto vs=shader(GL_VERTEX_SHADER,"#version 150\nin vec3 position;in vec3 normal;in vec2 uv;uniform mat4 mvp;uniform mat3 rotation;out vec3 n;out vec2 t;\n"
            "void main(){gl_Position=mvp*vec4(position,1.0);n=rotation*normal;t=uv;}\n");
        auto fs=shader(GL_FRAGMENT_SHADER,"#version 150\nin vec3 n;in vec2 t;uniform sampler2D image;uniform vec4 color;uniform float cutoff;out vec4 result;\n"
            "void main(){vec4 c=texture(image,t)*color;if(c.a<cutoff)discard;float l=abs(dot(normalize(n),normalize(vec3(0.3,0.5,1.0))));result=vec4(c.rgb*(0.3+0.7*l),1.0);}\n");
        program=glCreateProgram();glAttachShader(program,vs);glAttachShader(program,fs);
        glBindAttribLocation(program,0,"position");glBindAttribLocation(program,1,"normal");glBindAttribLocation(program,2,"uv");glLinkProgram(program);glDeleteShader(vs);glDeleteShader(fs);
        const unsigned char pixel[4]{255,255,255,255};glGenTextures(1,&white);glBindTexture(GL_TEXTURE_2D,white);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);upload=dirty=true;
    }
    void unrealize() {release();glDeleteProgram(program);glDeleteTextures(1,&white);program=white=0;}
    void vertexData(Part& p) {
        const auto count=p.rest.size()/3;std::vector<float> pos=p.rest,normal(count*3,0.f),out(count*8);
        if(frame>=0)for(std::size_t i=0;i<pos.size();++i)pos[i]+=p.deltas[frame][i];
        for(std::size_t t=0;t<p.index.size();t+=3){
            const float *a=&pos[p.index[t]*3],*b=&pos[p.index[t+1]*3],*c=&pos[p.index[t+2]*3];float u[3],v[3];for(int k=0;k<3;++k){u[k]=b[k]-a[k];v[k]=c[k]-a[k];}
            const float n[3]{u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]};for(int j=0;j<3;++j)for(int k=0;k<3;++k)normal[p.index[t+j]*3+k]+=n[k];
        }
        for(std::size_t v=0;v<count;++v){float* n=&normal[v*3];float l=std::hypot(n[0],n[1],n[2]);if(l<=0)l=1;
            for(int k=0;k<3;++k){out[v*8+k]=pos[v*3+k];out[v*8+3+k]=n[k]/l;}out[v*8+6]=p.uv[v*2];out[v*8+7]=p.uv[v*2+1];}
        glBindBuffer(GL_ARRAY_BUFFER,p.vbo);glBufferData(GL_ARRAY_BUFFER,static_cast<GLsizeiptr>(out.size()*sizeof(float)),out.data(),GL_DYNAMIC_DRAW);
    }
    void render(int width,int height) {
        glViewport(0,0,width,height);glClearColor(0.16f,0.17f,0.19f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);if(parts.empty()||!program)return;
        if(upload)for(auto& p:parts){
            glGenVertexArrays(1,&p.vao);glGenBuffers(1,&p.vbo);glGenBuffers(1,&p.ebo);glBindVertexArray(p.vao);glBindBuffer(GL_ARRAY_BUFFER,p.vbo);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,p.ebo);glBufferData(GL_ELEMENT_ARRAY_BUFFER,static_cast<GLsizeiptr>(p.index.size()*sizeof(unsigned)),p.index.data(),GL_STATIC_DRAW);
            for(GLuint a=0;a<3;++a){glEnableVertexAttribArray(a);glVertexAttribPointer(a,a==2?2:3,GL_FLOAT,GL_FALSE,8*sizeof(float),reinterpret_cast<const void*>(std::size_t(a==0?0:a==1?3:6)*sizeof(float)));}
            if(p.image){glGenTextures(1,&p.tex);glBindTexture(GL_TEXTURE_2D,p.tex);glPixelStorei(GL_UNPACK_ALIGNMENT,1);glPixelStorei(GL_UNPACK_ROW_LENGTH,gdk_pixbuf_get_rowstride(p.image)/4);
                glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,gdk_pixbuf_get_width(p.image),gdk_pixbuf_get_height(p.image),0,GL_RGBA,GL_UNSIGNED_BYTE,gdk_pixbuf_get_pixels(p.image));glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
                glGenerateMipmap(GL_TEXTURE_2D);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);}
        }
        if(upload||dirty)for(auto& p:parts)vertexData(p);
        upload=dirty=false;
        // Column-major: view = translate(0,0,-distance) · rotateX(pitch) · rotateY(yaw) · translate(-center).
        const float cy=std::cos(float(yaw)),sy=std::sin(float(yaw)),cp=std::cos(float(pitch)),sp=std::sin(float(pitch));
        const float r[9]{cy,sp*sy,-cp*sy, 0,cp,sp, sy,-sp*cy,cp*cy};
        const float distance=radius/std::tan(0.4f)*float(zoom),nearZ=std::max(distance-radius*2,distance*0.01f),farZ=distance+radius*2,f=1/std::tan(0.4f),aspect=float(width)/float(std::max(height,1));
        float t[3];for(int k=0;k<3;++k)t[k]=-(r[k]*center[0]+r[3+k]*center[1]+r[6+k]*center[2]);t[2]-=distance;
        const float view[16]{r[0],r[1],r[2],0,r[3],r[4],r[5],0,r[6],r[7],r[8],0,t[0],t[1],t[2],1};
        const float proj[16]{f/aspect,0,0,0, 0,f,0,0, 0,0,(farZ+nearZ)/(nearZ-farZ),-1, 0,0,2*farZ*nearZ/(nearZ-farZ),0};
        float mvp[16];for(int c=0;c<4;++c)for(int row=0;row<4;++row){mvp[c*4+row]=0;for(int k=0;k<4;++k)mvp[c*4+row]+=proj[k*4+row]*view[c*4+k];}
        glEnable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glUseProgram(program);
        glUniformMatrix4fv(glGetUniformLocation(program,"mvp"),1,GL_FALSE,mvp);glUniformMatrix3fv(glGetUniformLocation(program,"rotation"),1,GL_FALSE,r);glUniform1i(glGetUniformLocation(program,"image"),0);
        for(auto& p:parts){glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,p.tex?p.tex:white);glUniform4fv(glGetUniformLocation(program,"color"),1,p.color.data());glUniform1f(glGetUniformLocation(program,"cutoff"),p.cutoff);
            glBindVertexArray(p.vao);glDrawElements(GL_TRIANGLES,static_cast<GLsizei>(p.index.size()),GL_UNSIGNED_INT,nullptr);}
        glBindVertexArray(0);
    }
};
struct UI;
// One child at a time: argv only (no shell), stdout/stderr drained by IO watches, optional private log file.
struct Job {
    struct Stream {Job* job;bool error;} streams[2]{{this,false},{this,true}};
    UI* ui=nullptr;GPid pid=0;int status=0,open=3,log=-1;bool show=false,timedOut=false;guint timer=0,grace=0,watches[2]{};
    std::string out,err,partial;fs::path logPath;std::function<void(Job&)> done;
    bool ok() const {return !timedOut&&WIFEXITED(status)&&WEXITSTATUS(status)==0;}
    std::string failure() const {
        std::string why=timedOut?"timed out":WIFEXITED(status)?"exited with status "+std::to_string(WEXITSTATUS(status)):WIFSIGNALED(status)?"stopped by signal "+std::to_string(WTERMSIG(status)):"failed";
        auto tail=err.size()>800?err.substr(err.size()-800):err;while(!tail.empty()&&(tail.back()=='\n'||tail.back()=='\r'))tail.pop_back();
        return tail.empty()?why:why+": "+tail;
    }
};
struct UI {
    Options options; std::optional<Settings> settings;
    GtkWidget *window=nullptr,*root=nullptr,*status=nullptr,*book=nullptr,*apply=nullptr,*play=nullptr,*reload=nullptr,*restore=nullptr,*browse=nullptr,*progress=nullptr,*headerImage=nullptr;
    GtkWidget *mode=nullptr,*renderer=nullptr,*display=nullptr,*windowMode=nullptr,*resolution=nullptr,*language=nullptr,*bonusLanguage=nullptr,*audio=nullptr,*aa=nullptr,*camera=nullptr,*nativeGrid=nullptr;
    GtkWidget *launchPage=nullptr,*campaignPage=nullptr,*renderPage=nullptr,*audioPage=nullptr,*keysPage=nullptr,*joyPage=nullptr,*padPage=nullptr,*bonusPage=nullptr,*logsPage=nullptr;
    std::array<GtkWidget*,31> campaign{};std::array<GtkWidget*,5> bonus{};
    // Rendering (Contract D)
    GtkWidget *preset=nullptr,*backends=nullptr,*customGrid=nullptr,*packView=nullptr,*packUp=nullptr,*packDown=nullptr,*fpsLimit=nullptr;GtkListStore* packs=nullptr;
    std::vector<std::pair<Option,GtkWidget*>> optionWidgets;std::string capsNote="Load an installation to query engine capabilities.";
    // Mods (contract: one --mod ID per checked row, list order = priority)
    GtkWidget *modsPage=nullptr,*modView=nullptr,*modDetails=nullptr;GtkListStore* modStore=nullptr;std::vector<Mod> mods;
    // Mods 3-D viewer (docs/MODDING.md "Model viewer"); model rows: 0 shown text, 1 model key, 2 mod id ("" = original)
    GtkWidget *modelView=nullptr,*showOriginals=nullptr,*glArea=nullptr,*frameScale=nullptr,*viewInfo=nullptr;GtkListStore* modelStore=nullptr;
    Viewer viewer;std::map<std::string,std::vector<std::string>> originals; // per data root and mode: `kabus22 --list-models`
    fs::path shown;std::string shownKey;GSubprocess* exporting=nullptr;unsigned viewGeneration=0;bool listing=false;
    // Controller (Contract F)
    GtkWidget *padMode=nullptr,*deadzone=nullptr,*sensitivity=nullptr,*vibration=nullptr;std::vector<GtkWidget*> padButtons;
    // Installation (Contract C)
    GtkWidget *installInfo=nullptr,*source=nullptr,*chooseFile=nullptr,*chooseFolder=nullptr,*install=nullptr,*verify=nullptr,*repair=nullptr,*installResult=nullptr;
    // Updates (Contract B)
    GtkWidget *repository=nullptr,*channel=nullptr,*versions=nullptr,*checkUpdates=nullptr,*applyUpdate=nullptr,*rollback=nullptr,*latestInfo=nullptr,*notes=nullptr;
    std::string latest,previous;
    // Logs
    GtkWidget *logList=nullptr,*logText=nullptr;std::vector<fs::path> logPaths;
    std::unique_ptr<Job> job; GtkWidget* logView=nullptr; std::string log;
    bool populating=false;
    void message(const std::string& text) {gtk_label_set_text(GTK_LABEL(status),valid(text).c_str());}
    std::string rootText() const {return gtk_entry_get_text(GTK_ENTRY(root));}
    void availability() {
        bool loaded=settings.has_value(), idle=!job, hasRoot=!rootText().empty();
        gtk_widget_set_sensitive(apply,loaded&&idle);gtk_widget_set_sensitive(play,loaded&&idle);gtk_widget_set_sensitive(restore,loaded&&idle);
        gtk_widget_set_sensitive(reload,idle);gtk_widget_set_sensitive(root,idle);gtk_widget_set_sensitive(browse,idle);
        for(auto* w:{launchPage,campaignPage,renderPage,modsPage,audioPage,keysPage,joyPage,padPage,bonusPage})gtk_widget_set_sensitive(w,loaded&&idle);
        for(auto* w:{install,chooseFile,chooseFolder,source})gtk_widget_set_sensitive(w,idle);
        gtk_widget_set_sensitive(verify,idle&&hasRoot);gtk_widget_set_sensitive(repair,idle&&hasRoot&&gtk_entry_get_text_length(GTK_ENTRY(source))>0);
        for(auto* w:{repository,channel,checkUpdates})gtk_widget_set_sensitive(w,loaded&&idle);
        gtk_widget_set_sensitive(applyUpdate,loaded&&idle&&!latest.empty());gtk_widget_set_sensitive(rollback,loaded&&idle&&!previous.empty());
        if(!loaded||!idle)return;
        bool isBonus=selected(mode)=="bonus";
        gtk_widget_set_sensitive(campaignPage,!isBonus);gtk_widget_set_sensitive(audioPage,!isBonus);gtk_widget_set_sensitive(keysPage,!isBonus);gtk_widget_set_sensitive(joyPage,!isBonus);
        gtk_widget_set_sensitive(bonusPage,isBonus&&settings->bonusAvailable);
        gtk_widget_set_sensitive(display,!isBonus&&settings->originalCampaign);gtk_widget_set_sensitive(windowMode,!isBonus&&settings->originalCampaign);
        gtk_widget_set_sensitive(resolution,!isBonus&&settings->originalCampaign&&selected(display)=="native"&&selected(windowMode)!="borderless");
        gtk_widget_set_sensitive(language,!isBonus);gtk_widget_set_sensitive(bonusLanguage,isBonus&&settings->bonusAvailable);gtk_widget_set_sensitive(audio,!isBonus);gtk_widget_set_sensitive(aa,isBonus&&settings->bonusAvailable);
        gtk_widget_set_sensitive(camera,!isBonus&&settings->originalCampaign);
        gtk_widget_set_tooltip_text(display,settings->originalCampaign?"Native widescreen or original authored resolution services":"Patched/custom campaign and Bonus retain their authored display. Native overrides are inapplicable.");
        const auto& caps=settings->capabilities;bool custom=caps&&gtk_combo_box_get_active_id(GTK_COMBO_BOX(preset))&&selected(preset)=="custom";
        gtk_widget_set_sensitive(preset,caps.has_value());for(auto* w:{packView,packUp,packDown})gtk_widget_set_sensitive(w,caps.has_value());
        for(auto& [o,w]:optionWidgets)gtk_widget_set_sensitive(w,(custom||o.key=="fps_limit"||o.key=="seamless_doors")&&std::any_of(o.backends.begin(),o.backends.end(),[&](const std::string& b){return caps->available(b);}));
        bool modern=selected(padMode)=="modern";for(auto* w:{deadzone,sensitivity,vibration})gtk_widget_set_sensitive(w,modern);for(auto* w:padButtons)gtk_widget_set_sensitive(w,modern);
    }
    void populate() {
        populating=true;
        const auto& s=*settings;const auto& p=s.preferences;
        // Text languages reflect the installed data: es/ru/pl only with the community patch language files.
        gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(language));
        for(const auto& code:s.languages){static const std::pair<const char*,const char*> names[]={{"tr","Türkçe"},{"en","English"},{"de","Deutsch"},{"es","Español (patch)"},{"ru","Русский (patch)"},{"pl","Polski (patch)"}};
            for(auto [id,label]:names)if(code==id)gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(language),id,label);}
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(language),"saved","Keep saved language / game default");
        select(mode,p.mode);select(renderer,p.renderer);select(display,p.display);select(windowMode,p.window);select(language,p.language);select(bonusLanguage,p.bonusLanguage);select(audio,p.audioLanguage);select(aa,std::to_string(p.bonusAA));select(camera,p.camera);gtk_entry_set_text(GTK_ENTRY(resolution),p.resolution.c_str());
        for(int i=0;i<31;++i) {
            if(i==8||i==9||i==10||i==11||i==12||i==13||i==14||i>=25)gtk_spin_button_set_value(GTK_SPIN_BUTTON(campaign[i]),int(s.campaign[i])-(i==9||i==11?100:i==10?80:0));
            else select(campaign[i],std::to_string(s.campaign[i]));
        }
        if(s.bonusAvailable)for(int i=0;i<5;++i) {if(i==4)gtk_spin_button_set_value(GTK_SPIN_BUTTON(bonus[i]),s.bonus[i]);else select(bonus[i],std::to_string(s.bonus[i]));}
        const auto& c=s.controller;select(padMode,c.mode);gtk_spin_button_set_value(GTK_SPIN_BUTTON(deadzone),c.deadzone);gtk_spin_button_set_value(GTK_SPIN_BUTTON(sensitivity),c.sensitivity);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(vibration),c.vibration);
        for(std::size_t i=0;i<padButtons.size();++i)gtk_spin_button_set_value(GTK_SPIN_BUTTON(padButtons[i]),c.buttons[i].second);
        gtk_entry_set_text(GTK_ENTRY(repository),s.updates.repository.c_str());select(channel,s.updates.channel);
        populating=false;
        populateMods(p.mods,p.mods);populateRendering();
    }
    void populateRendering() {
        populating=true;
        gtk_container_foreach(GTK_CONTAINER(customGrid),+[](GtkWidget* w,gpointer){gtk_widget_destroy(w);},nullptr);optionWidgets.clear();gtk_list_store_clear(packs);
        gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(preset));gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(fpsLimit));gtk_widget_set_sensitive(fpsLimit,false);
        const Capabilities* caps=settings&&settings->capabilities?&*settings->capabilities:nullptr;
        if(!caps){gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(preset),"original","Original");select(preset,"original");gtk_label_set_text(GTK_LABEL(backends),valid(capsNote).c_str());populating=false;if(settings)availability();return;}
        for(const auto& name:caps->presets){std::string label=name;label[0]=g_ascii_toupper(label[0]);gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(preset),name.c_str(),label.c_str());}
        const auto& p=settings->preferences;select(preset,std::count(caps->presets.begin(),caps->presets.end(),p.preset)?p.preset:"original");
        std::string summary="Engine "+caps->version+" backends:";
        for(const auto& [name,state]:caps->backends)summary+="\n  "+name+": "+(state.first?"available":"unavailable")+(state.second.empty()?"":" — "+state.second);
        gtk_label_set_text(GTK_LABEL(backends),valid(summary).c_str());
        int y=0;
        for(int pass=0;pass<2;++pass)for(const auto& o:caps->options) {
            if(o.key=="backend"||o.key=="texture_packs"||(pass==1)!=(o.backends==std::vector<std::string>{"vulkan-rt"}))continue;
            if(pass==1&&y>=0&&!std::any_of(optionWidgets.begin(),optionWidgets.end(),[](const auto& w){return w.first.backends==std::vector<std::string>{"vulkan-rt"};})){auto* h=gtk_label_new(nullptr);gtk_label_set_markup(GTK_LABEL(h),"<b>Ray tracing (Vulkan RT only)</b>");gtk_label_set_xalign(GTK_LABEL(h),0);gtk_grid_attach(GTK_GRID(customGrid),h,0,y++,2,1);}
            // fps_limit has its own Rendering row and applies under every preset.
            const bool fps=o.key=="fps_limit";auto* w=fps?fpsLimit:gtk_combo_box_text_new();bool toggle=o.values==std::vector<std::string>{"0","1"};
            for(const auto& v:o.values)gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(w),v.c_str(),toggle?(v=="1"?"On":"Off"):!fps?v.c_str():v=="original"?"Original (campaign 72, Bonus 60)":v=="unlimited"?"Unlimited (vsync off)":(v+" FPS").c_str());
            auto saved=std::find_if(p.graphics.begin(),p.graphics.end(),[&](const auto& g){return g.first==o.key;});
            select(w,saved!=p.graphics.end()&&std::count(o.values.begin(),o.values.end(),saved->second)?saved->second:o.fallback);
            std::string tip="Engine key "+o.key+", default "+o.fallback+". Backends:";for(const auto& b:o.backends)tip+=" "+b;
            if(!fps)row(customGrid,y++,o.label.c_str(),w,tip.c_str());
            optionWidgets.emplace_back(o,w);
        }
        if(optionWidgets.empty())gtk_grid_attach(GTK_GRID(customGrid),note("This engine offers no custom graphics options."),0,0,2,1);
        std::vector<const Pack*> ordered;
        for(const auto& name:p.packs)for(const auto& pack:caps->packs)if(pack.name==name)ordered.push_back(&pack);
        for(const auto& pack:caps->packs)if(std::find(ordered.begin(),ordered.end(),&pack)==ordered.end())ordered.push_back(&pack);
        for(const auto* pack:ordered){
            bool on=pack->valid&&std::count(p.packs.begin(),p.packs.end(),pack->name);GtkTreeIter it;gtk_list_store_append(packs,&it);
            gtk_list_store_set(packs,&it,0,on,1,pack->name.c_str(),2,valid((pack->valid?"":"INVALID: ")+(pack->display.empty()?"":pack->display+" — ")+pack->detail).c_str(),3,pack->valid,-1);
        }
        gtk_widget_show_all(customGrid);populating=false;availability();
    }
    void readControls() {
        auto& s=*settings;auto& p=s.preferences;
        p.mode=selected(mode);p.renderer=selected(renderer);p.display=selected(display);p.window=selected(windowMode);p.language=selected(language);p.bonusLanguage=selected(bonusLanguage);p.audioLanguage=selected(audio);p.bonusAA=std::stoi(selected(aa));p.camera=selected(camera);p.resolution=gtk_entry_get_text(GTK_ENTRY(resolution));
        for(int i=0;i<31;++i) {
            int value=(i>=8&&i<=14)||i>=25?gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(campaign[i])):std::stoi(selected(campaign[i]));
            s.campaign[i]=static_cast<unsigned char>(value+(i==9||i==11?100:i==10?80:0));
        }
        if(s.bonusAvailable)for(int i=0;i<5;++i)s.bonus[i]=i==4?gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(bonus[i])):std::stoi(selected(bonus[i]));
        // Without capabilities the saved graphics choices stay untouched for a capable engine.
        if(s.capabilities) {
            p.preset=selected(preset);p.graphics.clear();p.packs.clear();
            for(const auto& [o,w]:optionWidgets){auto v=selected(w);if(v!=o.fallback)p.graphics.emplace_back(o.key,v);}
            GtkTreeIter it;for(bool more=gtk_tree_model_get_iter_first(GTK_TREE_MODEL(packs),&it);more;more=gtk_tree_model_iter_next(GTK_TREE_MODEL(packs),&it)){
                gboolean on=false;gchar* name=nullptr;gtk_tree_model_get(GTK_TREE_MODEL(packs),&it,0,&on,1,&name,-1);if(on)p.packs.emplace_back(name);g_free(name);
            }
        }
        auto& c=s.controller;c.mode=selected(padMode);c.vibration=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(vibration));
        c.deadzone=std::round(gtk_spin_button_get_value(GTK_SPIN_BUTTON(deadzone))*100)/100;c.sensitivity=std::round(gtk_spin_button_get_value(GTK_SPIN_BUTTON(sensitivity))*100)/100;
        for(std::size_t i=0;i<padButtons.size();++i)c.buttons[i].second=gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(padButtons[i]));
        p.mods=modIds(true);
        readUpdates();
    }
    void readUpdates() {settings->updates.repository=gtk_entry_get_text(GTK_ENTRY(repository));settings->updates.channel=selected(channel);}
    void loadRoot() {
        try {
            auto fresh=load(rootText(),options.engine);
            if(!options.renderer.empty())fresh.preferences.renderer=options.renderer;
            options.renderer.clear();
            settings=std::move(fresh);capsNote="Querying engine capabilities…";populate();
            auto icon=settings->root/"kabus22.png";if(fs::is_regular_file(icon))gtk_window_set_icon_from_file(GTK_WINDOW(window),icon.c_str(),nullptr);
            // Original setup's small wizard image, converted at install time; older installs stay plain.
            if(auto* small=k22::wizard::load(settings->root/"wizard-small-image.png")){gtk_image_set_from_pixbuf(GTK_IMAGE(headerImage),small);g_object_unref(small);}else gtk_image_clear(GTK_IMAGE(headerImage));
            std::string text="Settings loaded. Original game files and saves are untouched. Apply or Play commits private preferences.";
            for(const auto& w:settings->warnings)text+="\nWarning: "+w;
            message(text);describeInstallation();probe();
        } catch(const std::exception& error) {populating=false;settings.reset();latest.clear();previous.clear();capsNote="Load an installation to query engine capabilities.";describeInstallation();populateRendering();availability();message(error.what());}
    }
    void describeInstallation() {
        std::string text="Installation: "+rootText();
        try{auto active=k22::layout::active_version(rootText());text+=active.empty()?"\nLayout: flat (pre-0.2 install; the first update migrates it)":"\nActive version: "+active;}catch(const std::exception& e){text+=std::string("\n")+e.what();}
        text+=settings?"\nEngine: "+settings->engine.string():"\nNo loaded installation — use Install / Import below.";
        gtk_label_set_text(GTK_LABEL(installInfo),valid(text).c_str());
    }
    // ---- child processes ----
    static gboolean pipeReady(GIOChannel* channel,GIOCondition condition,gpointer data) {
        auto& stream=*static_cast<Job::Stream*>(data);auto& j=*stream.job;char buffer[4096];gsize count=0;GError* error=nullptr;
        for(int pass=0;;++pass) {
            auto result=g_io_channel_read_chars(channel,buffer,sizeof buffer,&count,&error);if(count)j.ui->received(j,stream.error,buffer,count);
            if(error){j.err+=std::string("\n")+error->message;g_error_free(error);break;}
            if(result==G_IO_STATUS_EOF)break;
            if(result==G_IO_STATUS_AGAIN||count==0){if(condition&(G_IO_ERR|G_IO_NVAL))break;return G_SOURCE_CONTINUE;}
            if(pass==3)return G_SOURCE_CONTINUE;
        }
        // closed() may destroy the job; nothing of it is touched afterwards.
        j.watches[stream.error]=0;j.ui->closed(j);return G_SOURCE_REMOVE;
    }
    void received(Job& j,bool error,const char* bytes,std::size_t size) {
        for(std::size_t offset=0;j.log>=0&&offset<size;){auto n=::write(j.log,bytes+offset,size-offset);if(n<0&&errno==EINTR)continue;if(n<=0)break;offset+=static_cast<std::size_t>(n);}
        if(j.show)appendLog(bytes,size);
        auto& sink=error?j.err:j.out;sink.append(bytes,size);if(error&&sink.size()>65536)sink.erase(0,sink.size()-65536);if(!error&&sink.size()>(4u<<20))sink.erase(0,sink.size()-(4u<<20));
        if(error)return;
        j.partial.append(bytes,size);
        for(auto end=j.partial.find('\n');end!=std::string::npos;end=j.partial.find('\n')){
            if(auto p=progressLine(std::string_view(j.partial).substr(0,end))){gtk_widget_show(progress);gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress),p->first/100.0);gtk_progress_bar_set_text(GTK_PROGRESS_BAR(progress),valid(std::to_string(p->first)+"%  "+p->second).c_str());}
            j.partial.erase(0,end+1);
        }
        if(j.partial.size()>65536)j.partial.clear();
    }
    void closed(Job& j) {
        if(--j.open)return;
        auto owned=std::move(job);if(owned->timer)g_source_remove(owned->timer);if(owned->grace)g_source_remove(owned->grace);if(owned->log>=0)::close(owned->log);
        gtk_widget_hide(progress);availability();
        try{if(owned->done)owned->done(*owned);}catch(const std::exception& e){message(e.what());}
        availability();refreshLogs();
    }
    void start(const char* kind,const std::vector<std::string>& args,const fs::path& working,bool show,guint seconds,std::function<void(Job&)> done) {
        binary(args.front());
        auto j=std::make_unique<Job>();j->ui=this;j->show=show;j->done=std::move(done);
        if(kind){
            auto [path,fd]=openLog(logDirectory(),kind);j->logPath=path;j->log=fd;
            std::string header="$";for(const auto& a:args){gchar* q=g_shell_quote(a.c_str());header+=" ";header+=q;g_free(q);}header+="\n";
            if(::write(fd,header.data(),header.size())<0){}
        }
        std::vector<gchar*> argv;for(const auto& a:args)argv.push_back(const_cast<gchar*>(a.c_str()));argv.push_back(nullptr);
        GError* error=nullptr;int out=-1,err=-1;
        if(!g_spawn_async_with_pipes(working.c_str(),argv.data(),nullptr,G_SPAWN_DO_NOT_REAP_CHILD,nullptr,nullptr,&j->pid,nullptr,&out,&err,&error)) {
            std::string why=error?error->message:"Cannot spawn "+args.front();if(error)g_error_free(error);if(j->log>=0)::close(j->log);throw std::runtime_error(why);
        }
        if(show){log.clear();appendLog("",0);}
        for(int i=0;i<2;++i){
            auto* channel=g_io_channel_unix_new(i?err:out);g_io_channel_set_close_on_unref(channel,true);g_io_channel_set_encoding(channel,nullptr,nullptr);g_io_channel_set_buffered(channel,false);g_io_channel_set_flags(channel,static_cast<GIOFlags>(g_io_channel_get_flags(channel)|G_IO_FLAG_NONBLOCK),nullptr);
            j->watches[i]=g_io_add_watch(channel,static_cast<GIOCondition>(G_IO_IN|G_IO_HUP|G_IO_ERR|G_IO_NVAL),pipeReady,&j->streams[i]);g_io_channel_unref(channel);
        }
        // A detached grandchild (e.g. settings opened from the installer) may keep the pipes open: stop reading shortly after our child exits.
        g_child_watch_add(j->pid,+[](GPid pid,gint result,gpointer data){
            auto& j=*static_cast<Job*>(data);g_spawn_close_pid(pid);j.pid=0;j.status=result;
            if(j.open>1)j.grace=g_timeout_add(500,+[](gpointer d)->gboolean{auto& j=*static_cast<Job*>(d);j.grace=0;for(auto& w:j.watches)if(w){g_source_remove(w);w=0;}j.open=1;j.ui->closed(j);return G_SOURCE_REMOVE;},data);
            j.ui->closed(j);
        },j.get());
        if(seconds)j->timer=g_timeout_add_seconds(seconds,+[](gpointer data)->gboolean{auto& j=*static_cast<Job*>(data);j.timer=0;if(j.pid>0){j.timedOut=true;::kill(j.pid,SIGKILL);}return G_SOURCE_REMOVE;},j.get());
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress),0);job=std::move(j);availability();
    }
    void appendLog(const char* bytes,std::size_t size) {
        log.append(bytes,size);if(log.size()>65536)log.erase(0,log.size()-65536);setText(logView,log);
    }
    // ---- engine ----
    void probe() {
        try {
            start(nullptr,{settings->engine.string(),"--list-capabilities"},settings->root,false,20,[](Job& j){
                auto& ui=*j.ui;if(!ui.settings)return;
                try{if(!j.ok())throw std::runtime_error("engine "+j.failure());ui.settings->capabilities=parseCapabilities(j.out);ui.capsNote.clear();}
                catch(const std::exception& e){ui.settings->capabilities.reset();ui.capsNote=std::string("Capabilities unavailable: ")+e.what()+"\nPresets, custom options and texture packs are disabled; the engine starts with original graphics.";}
                ui.populateRendering();ui.queryStatus();
            });
        } catch(const std::exception& e){capsNote=std::string("Capabilities unavailable: ")+e.what();populateRendering();}
    }
    void commit(bool launch) {
        try {
            if(!settings)throw std::runtime_error("Load an existing native installation first");
            readControls();auto args=command(*settings,options.frames);save(*settings);
            if(!launch){message("Settings applied to private files. Restart the game to use them.");return;}
            const auto working=settings->root/"game"/(settings->preferences.mode=="bonus"?"Kabus 22 Mode":"");
            start("engine",args,working,true,0,[](Job& j){
                auto& ui=*j.ui;
                // Refresh game-written settings before offering another Apply; keep startup diagnostics visible.
                ui.loadRoot();
                if(ui.settings){
                    if(j.ok())ui.message("Game exited normally. Settings refreshed from private game files. Log: "+j.logPath.string());
                    else ui.message("Native game "+j.failure()+"\nSee Game output below or "+j.logPath.string()+"; no API fallback was attempted.");
                }
            });
            message("Native game running. Close it before changing settings or closing the launcher. Log: "+job->logPath.string());
        } catch(const std::exception& error) {message(std::string("Apply / launch failed: ")+error.what()+"\nReload before retrying; any successfully committed private file is retained.");availability();}
    }
    void defaults() {
        if(!settings)return;
        // Keep unsaved edits on every other page; restore only the original game records.
        readControls();
        auto extra=settings->campaign.size()==32?std::optional<unsigned char>(settings->campaign[31]):std::nullopt;
        settings->campaign=settings->defaults;if(extra&&settings->campaign.size()==31)settings->campaign.push_back(*extra);
        settings->bonus=settings->bonusDefaults;
        if(settings->bonusAvailable&&settings->localeSource.bytes)settings->preferences.bonusLanguage=bonusMarkerLanguage(*settings->localeSource.bytes);
        populate();message("Supplied campaign / Bonus record defaults and Bonus locale restored in the controls only. Apply or Play to save; launcher API/display/AA choices are unchanged.");
    }
    // ---- installation (Contract C) ----
    void runSetup(std::vector<std::string> extra,const char* what,std::function<void(Job&)> done) {
        try{auto args=std::vector<std::string>{(tools()/"kabus22-setup").string()};args.insert(args.end(),extra.begin(),extra.end());start("setup",args,g_get_home_dir(),false,0,std::move(done));message(std::string(what)+" running. Log: "+job->logPath.string());}
        catch(const std::exception& e){message(std::string(what)+" failed: "+e.what());}
    }
    void chooseSource(bool folder) {
        auto* dialog=gtk_file_chooser_dialog_new(folder?"Choose original Windows Kabus 22 folder (K22.exe / K22.wrs)":"Choose original Kabus 22 ISO or setup.exe",GTK_WINDOW(window),folder?GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER:GTK_FILE_CHOOSER_ACTION_OPEN,"_Cancel",GTK_RESPONSE_CANCEL,"_Select",GTK_RESPONSE_ACCEPT,nullptr);
        if(!folder){auto* filter=gtk_file_filter_new();gtk_file_filter_set_name(filter,"ISO image or setup.exe");gtk_file_filter_add_pattern(filter,"*.[iI][sS][oO]");gtk_file_filter_add_pattern(filter,"[sS][eE][tT][uU][pP].[eE][xX][eE]");gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog),filter);}
        if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT){gchar* path=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));gtk_entry_set_text(GTK_ENTRY(source),path);g_free(path);}
        gtk_widget_destroy(dialog);availability();
    }
    void installGame() {
        std::vector<std::string> extra;std::string media=gtk_entry_get_text(GTK_ENTRY(source));if(!media.empty())extra={"--source",media};
        runSetup(extra,"Installer",[](Job& j){auto& ui=*j.ui;ui.loadRoot();ui.message(j.ok()?"Installer closed. Installation reloaded.":"Installer "+j.failure());});
    }
    void verifyAssets() {
        gtk_label_set_text(GTK_LABEL(installResult),"Verifying every installed file against .kabus22-inventory.json…");
        runSetup({"--verify","--install-dir",rootText(),"--json"},"Verification",[](Job& j){
            auto& ui=*j.ui;std::string text;
            try{
                if(!j.ok()&&!(WIFEXITED(j.status)&&WEXITSTATUS(j.status)==3))throw std::runtime_error("kabus22-setup "+j.failure());
                auto v=parseVerify(j.out);
                text=v.ok?"All "+std::to_string(v.checked)+" inventory files verified.":std::to_string(v.checked)+" files checked: "+std::to_string(v.missing.size())+" missing, "+std::to_string(v.modified.size())+" modified. Repair restores game files from the original media.";
                std::size_t shown=0;for(auto* list:{&v.missing,&v.modified})for(const auto& path:*list)if(shown++<40)text+=std::string(list==&v.missing?"\n  missing: ":"\n  modified: ")+path;
                if(shown>40)text+="\n  … "+std::to_string(shown-40)+" more (see log)";
            }catch(const std::exception& e){text=std::string("Verification failed: ")+e.what();}
            gtk_label_set_text(GTK_LABEL(ui.installResult),valid(text).c_str());ui.message("Verification finished. Log: "+j.logPath.string());
        });
    }
    void repairInstall() {
        gtk_label_set_text(GTK_LABEL(installResult),"Repairing game files from original media…");
        runSetup({"--repair","--install-dir",rootText(),"--source",gtk_entry_get_text(GTK_ENTRY(source)),"--unattended"},"Repair",[](Job& j){
            auto& ui=*j.ui;auto text=j.ok()?std::string("Repair finished. Run Verify assets to confirm."):"Repair "+j.failure();
            gtk_label_set_text(GTK_LABEL(ui.installResult),valid(text).c_str());ui.loadRoot();ui.message(text+"\nLog: "+j.logPath.string());
        });
    }
    // ---- updates (Contract B) ----
    std::vector<std::string> updater(std::initializer_list<const char*> command) {
        std::vector<std::string> args{(tools()/"kabus22-updater").string(),"--install-dir",settings->root.string()};args.insert(args.end(),command.begin(),command.end());return args;
    }
    void queryStatus() {
        if(!settings)return;
        try{start(nullptr,updater({"--status","--json"}),settings->root,false,30,[](Job& j){
            auto& ui=*j.ui;std::string text;ui.previous.clear();
            try{if(!j.ok())throw std::runtime_error("kabus22-updater "+j.failure());auto s=parseStatus(j.out);ui.previous=s.previous;
                text="Active: "+(s.active.empty()?std::string("flat legacy layout"):s.active)+"   Previous: "+(s.previous.empty()?std::string("none"):s.previous)+"\nInstalled:";for(const auto& v:s.installed)text+=" "+v;}
            catch(const std::exception& e){text=std::string("Update status unavailable: ")+e.what();}
            gtk_label_set_text(GTK_LABEL(ui.versions),valid("Launcher "+std::string(K22_VERSION)+"\n"+text).c_str());
        });}
        catch(const std::exception& e){gtk_label_set_text(GTK_LABEL(versions),valid(std::string("Update status unavailable: ")+e.what()).c_str());}
    }
    void check() {
        try{
            readUpdates();saveUpdates(*settings);latest.clear();
            const auto& u=settings->updates;
            start("updater",updater({"--check","--json","--channel",u.channel.c_str(),"--repository",u.repository.c_str()}),settings->root,false,0,[](Job& j){
                auto& ui=*j.ui;std::string text;
                try{if(!j.ok())throw std::runtime_error("kabus22-updater "+j.failure());auto c=parseCheck(j.out);
                    text="Current "+c.current+" — latest "+(c.latest.empty()?std::string("none published"):c.latest)+" on "+c.channel+" from "+c.repository+(c.published.empty()?"":" ("+c.published+")");
                    text+=c.available?"\nAn update is available. Apply update installs it beside the current version; Roll back restores the previous one.":"\nNo newer version is available.";
                    if(c.available)ui.latest=c.latest;
                    setText(ui.notes,c.notes.empty()?"(no release notes)":c.notes);}
                catch(const std::exception& e){text=std::string("Update check failed: ")+e.what();setText(ui.notes,"");}
                gtk_label_set_text(GTK_LABEL(ui.latestInfo),valid(text).c_str());ui.message("Update check finished. Log: "+j.logPath.string());
            });
            gtk_label_set_text(GTK_LABEL(latestInfo),"Checking…");
        }catch(const std::exception& e){message(std::string("Update check failed: ")+e.what());availability();}
    }
    void updateAction(bool undo) {
        try{
            readUpdates();saveUpdates(*settings);const auto& u=settings->updates;
            auto args=undo?updater({"--rollback"}):updater({"--apply","--version",latest.c_str(),"--channel",u.channel.c_str(),"--repository",u.repository.c_str()});
            start("updater",args,settings->root,false,0,[undo](Job& j){
                auto& ui=*j.ui;ui.latest.clear();
                if(j.ok())ui.restart(); // returns only when the installed launcher cannot be started
                auto text=j.ok()?std::string(undo?"Rolled back to the previous version.":"Update installed.")+" The game uses it on next Play; restart the launcher to use its new version.":std::string(undo?"Rollback ":"Update ")+j.failure()+"\nNothing was changed on failure.";
                gtk_label_set_text(GTK_LABEL(ui.latestInfo),valid(text).c_str());ui.loadRoot();ui.message(text+"\nLog: "+j.logPath.string());
            });
            message(std::string(undo?"Rolling back":"Downloading and verifying update")+"… Log: "+job->logPath.string());
        }catch(const std::exception& e){message(std::string("Update failed: ")+e.what());availability();}
    }
    // After an update or rollback root/bin follows root/current, so this execs the now-active version's launcher in place.
    void restart() {
        const auto next=settings->root/"bin/kabus22-launcher";
        std::vector<std::string> args{next.string(),"--install-dir",settings->root.string()};
        if(!options.engine.empty())args.insert(args.end(),{"--engine",options.engine.string()});
        if(!options.renderer.empty())args.insert(args.end(),{"--renderer",options.renderer});
        std::vector<char*> argv;for(auto& a:args)argv.push_back(a.data());argv.push_back(nullptr);
        ::execv(next.c_str(),argv.data());
        std::cerr<<"kabus22-launcher: cannot restart into "<<next<<": "<<std::strerror(errno)<<'\n';
    }
    // ---- logs ----
    void refreshLogs() {
        gtk_container_foreach(GTK_CONTAINER(logList),+[](GtkWidget* w,gpointer){gtk_widget_destroy(w);},nullptr);
        logPaths=listLogs(logDirectory());std::reverse(logPaths.begin(),logPaths.end());
        for(const auto& path:logPaths){auto* l=gtk_label_new(path.filename().c_str());gtk_label_set_xalign(GTK_LABEL(l),0);gtk_list_box_insert(GTK_LIST_BOX(logList),l,-1);}
        if(logPaths.empty())setText(logText,"No logs yet in "+logDirectory().string());
        gtk_widget_show_all(logList);
    }
    void showLog(int index) {
        if(index<0||index>=static_cast<int>(logPaths.size()))return;
        std::ifstream file(logPaths[index],std::ios::binary|std::ios::ate);if(!file){setText(logText,"Cannot read "+logPaths[index].string());return;}
        std::streamoff size=file.tellg(),start=std::max<std::streamoff>(0,size-262144);file.seekg(start);std::string bytes(static_cast<std::size_t>(size-start),'\0');file.read(bytes.data(),static_cast<std::streamsize>(bytes.size()));
        setText(logText,(start?"[… earlier output omitted]\n":"")+bytes);
    }
    void openFolder(const fs::path& dir) {
        g_mkdir_with_parents(dir.c_str(),0700);gchar* uri=g_filename_to_uri(dir.c_str(),nullptr,nullptr);GError* error=nullptr;
        if(!uri||!gtk_show_uri_on_window(GTK_WINDOW(window),uri,GDK_CURRENT_TIME,&error)){message(std::string("Cannot open ")+dir.string()+(error?std::string(": ")+error->message:""));if(error)g_error_free(error);}g_free(uri);
    }
    // ---- mods ----
    std::vector<std::string> modIds(bool enabledOnly) {
        std::vector<std::string> ids;GtkTreeIter it;
        for(bool more=gtk_tree_model_get_iter_first(GTK_TREE_MODEL(modStore),&it);more;more=gtk_tree_model_iter_next(GTK_TREE_MODEL(modStore),&it)){
            gboolean on=false;gchar* id=nullptr;gtk_tree_model_get(GTK_TREE_MODEL(modStore),&it,0,&on,6,&id,-1);if(on||!enabledOnly)ids.emplace_back(id);g_free(id);
        }
        return ids;
    }
    std::string selectedMod() {
        GtkTreeIter it;GtkTreeModel* model=nullptr;if(!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(modView)),&model,&it))return {};
        gchar* id=nullptr;gtk_tree_model_get(model,&it,6,&id,-1);std::string result=id;g_free(id);return result;
    }
    // Listed order: `order` first, then newly found mods by id; only valid mods can be checked.
    void populateMods(const std::vector<std::string>& order,const std::vector<std::string>& enabled) {
        mods=listMods(modsRoot());
        auto rank=[&](const Mod& m){return std::find(order.begin(),order.end(),m.id)-order.begin();};
        std::stable_sort(mods.begin(),mods.end(),[&](const Mod& a,const Mod& b){return rank(a)<rank(b);});
        gtk_list_store_clear(modStore);
        for(const auto& m:mods){
            GtkTreeIter it;gtk_list_store_append(modStore,&it);
            gtk_list_store_set(modStore,&it,0,m.valid&&std::count(enabled.begin(),enabled.end(),m.id),1,valid((m.name.empty()?m.id:m.name)+" ("+m.id+")").c_str(),2,valid(m.version).c_str(),3,valid(m.author).c_str(),
                4,valid(m.valid?"OK — "+m.detail:"INVALID: "+m.detail).c_str(),5,m.valid,6,m.id.c_str(),-1);
        }
        describeMod();
    }
    void refreshMods() {populateMods(modIds(false),modIds(true));}
    void describeMod() {
        const auto id=selectedMod();std::string text="Select a mod for its details. Mods folder: "+modsRoot().string();
        for(const auto& m:mods)if(m.id==id){
            text=(m.name.empty()?m.id:m.name)+(m.version.empty()?"":" "+m.version)+(m.author.empty()?"":" — by "+m.author)+(m.description.empty()?"":"\n"+m.description);
            text+=(m.valid?"\n"+m.detail+" under data/":"\nInvalid: "+m.detail)+"\nFolder: "+(modsRoot()/m.id).string();
        }
        std::map<std::string,std::string> owner;std::vector<std::string> clashes;
        for(const auto& on:modIds(true))for(const auto& m:mods)if(m.id==on)for(const auto& file:m.files){auto [at,fresh]=owner.emplace(file,on);if(!fresh)clashes.push_back(file+" provided by "+at->second+" and "+on+" — "+at->second+" wins");}
        if(!clashes.empty()){text+="\n\nConflicts among enabled mods:";for(std::size_t i=0;i<clashes.size()&&i<20;++i)text+="\n  "+clashes[i];if(clashes.size()>20)text+="\n  … "+std::to_string(clashes.size()-20)+" more";}
        gtk_label_set_text(GTK_LABEL(modDetails),valid(text).c_str());
    }
    std::string newMod() {
        auto* dialog=gtk_dialog_new_with_buttons("New mod",GTK_WINDOW(window),GTK_DIALOG_MODAL,"_Cancel",GTK_RESPONSE_CANCEL,"C_reate",GTK_RESPONSE_ACCEPT,nullptr);
        auto* grid=gtk_grid_new();gtk_grid_set_row_spacing(GTK_GRID(grid),9);gtk_grid_set_column_spacing(GTK_GRID(grid),18);gtk_container_set_border_width(GTK_CONTAINER(grid),18);
        auto* id=gtk_entry_new();auto* name=gtk_entry_new();gtk_entry_set_placeholder_text(GTK_ENTRY(id),"folder name, e.g. my-mod");gtk_entry_set_placeholder_text(GTK_ENTRY(name),"shown in the list");for(auto* e:{id,name})gtk_entry_set_width_chars(GTK_ENTRY(e),32);
        row(grid,0,"Mod _id",id);row(grid,1,"_Name",name);gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),grid);gtk_widget_show_all(dialog);
        if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT){
            const std::string folder=gtk_entry_get_text(GTK_ENTRY(id));
            try{createMod(modsRoot(),folder,gtk_entry_get_text(GTK_ENTRY(name)));refreshMods();selectMod(folder);message("Created "+(modsRoot()/folder).string()+". Put replacement files under its data/ folder with their game names (Export game files… gives you the originals), then enable it.");gtk_widget_destroy(dialog);return folder;}
            catch(const std::exception& e){message(std::string("New mod failed: ")+e.what());}
        }
        gtk_widget_destroy(dialog);return {};
    }
    void installZip() {
        auto* dialog=gtk_file_chooser_dialog_new("Install mod from .zip",GTK_WINDOW(window),GTK_FILE_CHOOSER_ACTION_OPEN,"_Cancel",GTK_RESPONSE_CANCEL,"_Install",GTK_RESPONSE_ACCEPT,nullptr);
        auto* filter=gtk_file_filter_new();gtk_file_filter_set_name(filter,"Zip archive");gtk_file_filter_add_pattern(filter,"*.[zZ][iI][pP]");gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog),filter);
        if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT){
            gchar* path=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
            try{auto id=installMod(modsRoot(),path);refreshMods();message("Installed mod "+id+". Check it in the list, then Apply or Play.");}catch(const std::exception& e){message(std::string("Mod install failed: ")+e.what());}
            g_free(path);
        }
        gtk_widget_destroy(dialog);
    }
    void removeMod() {
        const auto id=selectedMod();if(id.empty()){message("Select the mod to remove.");return;}
        auto* ask=gtk_message_dialog_new(GTK_WINDOW(window),GTK_DIALOG_MODAL,GTK_MESSAGE_QUESTION,GTK_BUTTONS_NONE,"Remove mod “%s”?",id.c_str());
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(ask),"This permanently deletes %s and everything in it.",(modsRoot()/id).c_str());
        gtk_dialog_add_buttons(GTK_DIALOG(ask),"_Cancel",GTK_RESPONSE_CANCEL,"_Remove",GTK_RESPONSE_ACCEPT,nullptr);
        if(gtk_dialog_run(GTK_DIALOG(ask))==GTK_RESPONSE_ACCEPT){
            try{fs::remove_all(modsRoot()/id);refreshMods();message("Removed mod "+id+".");}catch(const std::exception& e){message(std::string("Remove failed: ")+e.what());}
        }
        gtk_widget_destroy(ask);
    }
    void exportFiles() {
        auto* dialog=gtk_file_chooser_dialog_new("Export game files to a new or empty folder",GTK_WINDOW(window),GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,"_Cancel",GTK_RESPONSE_CANCEL,"_Export",GTK_RESPONSE_ACCEPT,nullptr);
        gtk_file_chooser_set_create_folders(GTK_FILE_CHOOSER(dialog),true);
        std::string dir;if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT){gchar* path=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));dir=path?path:"";g_free(path);}
        gtk_widget_destroy(dialog);if(dir.empty()||!settings)return;
        try{
            const bool bonus=selected(mode)=="bonus";if(bonus&&!settings->bonusAvailable)throw std::runtime_error("Bonus data is not installed");
            std::vector<std::string> args{settings->engine.string(),"--export-files",dir,"--data",(settings->root/"game"/(bonus?"Kabus 22 Mode":"")).string()};if(bonus)args.insert(args.end(),{"--mode","bonus"});
            start("export",args,settings->root,false,0,[dir](Job& j){
                auto& ui=*j.ui;auto text=j.out.empty()?j.err:j.out;while(!text.empty()&&(text.back()=='\n'||text.back()=='\r'))text.pop_back();
                if(auto line=text.find_last_of('\n');line!=std::string::npos)text.erase(0,line+1);
                ui.message(j.ok()?"Exported game files to "+dir+(text.empty()?"":" ("+text+")")+". Copy the files you change into a mod's data/ folder, keeping their paths.":"Export "+j.failure()+"\nLog: "+j.logPath.string());
            });
            message("Exporting "+std::string(bonus?"Bonus":"campaign")+" game files to "+dir+"… Log: "+job->logPath.string());
        }catch(const std::exception& e){message(std::string("Export failed: ")+e.what());availability();}
    }
    // ---- mods: 3-D viewer (docs/MODDING.md "Model viewer") ----
    void selectMod(const std::string& id) {
        GtkTreeIter it;for(bool more=gtk_tree_model_get_iter_first(GTK_TREE_MODEL(modStore),&it);more;more=gtk_tree_model_iter_next(GTK_TREE_MODEL(modStore),&it)){
            gchar* each=nullptr;gtk_tree_model_get(GTK_TREE_MODEL(modStore),&it,6,&each,-1);bool hit=id==each;g_free(each);if(hit){gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(modView)),&it);return;}
        }
    }
    std::pair<std::string,std::string> selectedModel() {
        GtkTreeIter it;GtkTreeModel* model=nullptr;if(!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(modelView)),&model,&it))return {};
        gchar *key=nullptr,*id=nullptr;gtk_tree_model_get(model,&it,1,&key,2,&id,-1);std::pair<std::string,std::string> r{key,id};g_free(key);g_free(id);return r;
    }
    bool bonusMode() {return gtk_combo_box_get_active_id(GTK_COMBO_BOX(mode))&&selected(mode)=="bonus";}
    std::string dataKey() {return settings->root.string()+(bonusMode()?"#bonus":"");}
    std::vector<std::string> engineArgs(std::initializer_list<std::string> first) {
        const bool bonus=bonusMode();std::vector<std::string> args{settings->engine.string()};args.insert(args.end(),first);
        args.insert(args.end(),{"--data",(settings->root/"game"/(bonus?"Kabus 22 Mode":"")).string()});if(bonus)args.insert(args.end(),{"--mode","bonus"});return args;
    }
    static std::string tail(std::string text) {while(!text.empty()&&(text.back()=='\n'||text.back()=='\r'))text.pop_back();return text.size()>600?"…"+text.substr(text.size()-600):text;}
    static std::string bullets(const std::vector<std::string>& lines) {std::string s;for(std::size_t i=0;i<lines.size()&&i<30;++i)s+="\n  "+lines[i];if(lines.size()>30)s+="\n  … "+std::to_string(lines.size()-30)+" more";return s;}
    bool confirm(const std::string& title,const std::string& text,const char* action) {
        auto* ask=gtk_message_dialog_new(GTK_WINDOW(window),GTK_DIALOG_MODAL,GTK_MESSAGE_QUESTION,GTK_BUTTONS_NONE,"%s",valid(title).c_str());
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(ask),"%s",valid(text).c_str());gtk_dialog_add_buttons(GTK_DIALOG(ask),"_Cancel",GTK_RESPONSE_CANCEL,action,GTK_RESPONSE_ACCEPT,nullptr);
        const bool yes=gtk_dialog_run(GTK_DIALOG(ask))==GTK_RESPONSE_ACCEPT;gtk_widget_destroy(ask);return yes;
    }
    void viewNote(const std::string& text) {gtk_label_set_text(GTK_LABEL(viewInfo),valid(text).c_str());}
    // Viewer engine runs go beside the single Job so the page stays usable; argv only, cwd = installation.
    using Done=std::function<void(bool,const std::string&,const std::string&)>;
    GSubprocess* spawn(const std::vector<std::string>& args,Done done) {
        binary(args.front());std::vector<const gchar*> argv;for(const auto& a:args)argv.push_back(a.c_str());argv.push_back(nullptr);
        auto* launcher=g_subprocess_launcher_new(static_cast<GSubprocessFlags>(G_SUBPROCESS_FLAGS_STDOUT_PIPE|G_SUBPROCESS_FLAGS_STDERR_PIPE));g_subprocess_launcher_set_cwd(launcher,settings->root.c_str());
        GError* error=nullptr;auto* process=g_subprocess_launcher_spawnv(launcher,argv.data(),&error);g_object_unref(launcher);
        if(!process){std::string why=error->message;g_error_free(error);throw std::runtime_error(why);}
        g_subprocess_communicate_async(process,nullptr,nullptr,+[](GObject* source,GAsyncResult* result,gpointer data){
            std::unique_ptr<Done> done(static_cast<Done*>(data));auto* process=G_SUBPROCESS(source);GBytes *out=nullptr,*err=nullptr;
            const bool ok=g_subprocess_communicate_finish(process,result,&out,&err,nullptr)&&g_subprocess_get_if_exited(process)&&g_subprocess_get_exit_status(process)==0;
            auto text=[](GBytes* b){gsize n=0;const auto* p=b?static_cast<const char*>(g_bytes_get_data(b,&n)):nullptr;std::string s(p?p:"",p?n:0);if(b)g_bytes_unref(b);return s;};
            const auto o=text(out),e=text(err);g_object_unref(process);(*done)(ok,o,e);
        },new Done(std::move(done)));
        return process;
    }
    // Rows: the selected mod's model files, then (Show originals) every game model; key/id reselects a row (default: the current one).
    void refreshModels(std::string key={},std::string id={}) {
        if(!modelStore)return;
        if(key.empty())std::tie(key,id)=selectedModel();
        gtk_list_store_clear(modelStore);if(!settings)return;
        const auto mod=selectedMod();std::set<std::string> replaced;GtkTreeIter it,chosen;bool found=false;
        auto add=[&](const std::string& text,const std::string& k,const std::string& source){gtk_list_store_append(modelStore,&it);gtk_list_store_set(modelStore,&it,0,valid(text).c_str(),1,k.c_str(),2,source.c_str(),-1);if(k==key&&source==id){chosen=it;found=true;}};
        for(const auto& m:mods)if(m.id==mod)for(const auto& f:m.files)if(auto k=modelKey(f);!k.empty()){replaced.insert(k);add(f+"   (mod "+mod+")",k,mod);}
        if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(showOriginals))){
            auto list=originals.find(dataKey());
            if(list==originals.end())listModels();
            else for(const auto& name:list->second)add(name+(replaced.count(name)?"   ← replaced by "+mod:""),name,"");
        }
        if(found){gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(modelView)),&chosen);auto* path=gtk_tree_model_get_path(GTK_TREE_MODEL(modelStore),&chosen);gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(modelView),path,nullptr,true,0.5f,0);gtk_tree_path_free(path);}
    }
    void listModels() {
        if(listing)return;
        listing=true;const auto data=dataKey();viewNote("Listing the game's models…");
        try{spawn(engineArgs({"--list-models"}),[this,data](bool ok,const std::string& out,const std::string& err){
            listing=false;
            if(!ok){viewNote("Cannot list the game's models: "+tail(err.empty()?out:err));gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(showOriginals),false);return;}
            auto& names=originals[data];names.clear();
            for(std::size_t start=0;start<out.size();){auto end=std::min(out.find('\n',start),out.size());auto line=out.substr(start,end-start);start=end+1;if(auto tab=line.find('\t');tab&&!line.empty())names.push_back(line.substr(0,tab));}
            viewNote(std::to_string(names.size())+" game models. Select one to view it.");refreshModels();
        });}catch(const std::exception& e){listing=false;viewNote(std::string("Cannot list the game's models: ")+e.what());}
    }
    // Reuses the cached export when it is newer than the engine and every file of the mod; otherwise exports into a
    // private .part folder and renames it into place, so an interrupted export is never reused.
    void showModel() {
        auto [key,id]=selectedModel();if(key.empty()||!settings)return;
        if(exporting){g_subprocess_force_exit(exporting);g_clear_object(&exporting);}
        const unsigned generation=++viewGeneration;const auto cache=viewerCache(id,key,bonusMode());const auto gltf=cache/(fs::path(key).stem().string()+".gltf");
        auto stamp=[](const fs::path& p){std::error_code ec;auto t=fs::last_write_time(p,ec);return ec?fs::file_time_type::min():t;};
        auto newest=stamp(settings->engine);
        if(!id.empty()){std::error_code ec;for(fs::recursive_directory_iterator it(modsRoot()/id/"data",ec),end;!ec&&it!=end;it.increment(ec))newest=std::max(newest,stamp(it->path()));}
        if(fs::is_regular_file(gltf)&&stamp(gltf)>=newest){showGltf(gltf,key);return;}
        const auto part=cache.parent_path()/(".part-"+std::to_string(::getpid())+"-"+std::to_string(generation));
        std::error_code ec;fs::remove_all(part,ec);fs::create_directories(part,ec);
        auto args=engineArgs({"--export-model",key,part.string()});if(!id.empty())args.insert(args.end(),{"--mod",id});
        viewNote("Exporting "+key+(id.empty()?"":" with mod "+id)+"…");
        try{exporting=G_SUBPROCESS(g_object_ref(spawn(args,[this,generation,part,cache,gltf,key](bool ok,const std::string& out,const std::string& err){
            std::error_code ec;if(generation!=viewGeneration){fs::remove_all(part,ec);return;}
            g_clear_object(&exporting);std::string why=ok?std::string():tail(err.empty()?out:err);
            if(ok){fs::remove_all(cache,ec);fs::rename(part,cache,ec);if(ec)why=ec.message();}
            if(!why.empty()){fs::remove_all(part,ec);viewer.clear();shown.clear();gtk_gl_area_queue_render(GTK_GL_AREA(glArea));viewNote("Export of "+key+" failed: "+why);return;}
            showGltf(gltf,key);
        })));}catch(const std::exception& e){fs::remove_all(part,ec);viewNote(std::string("Export failed: ")+e.what());}
    }
    void showGltf(const fs::path& gltf,const std::string& key) {
        try{viewer.load(gltf);}catch(const std::exception& e){viewer.clear();shown.clear();gtk_gl_area_queue_render(GTK_GL_AREA(glArea));viewNote(e.what());return;}
        shown=gltf.parent_path();shownKey=key;viewer.reset();
        const int frames=static_cast<int>(viewer.frames.size());gtk_range_set_range(GTK_RANGE(frameScale),0,std::max(frames-1,1));gtk_range_set_value(GTK_RANGE(frameScale),0);gtk_widget_set_sensitive(frameScale,frames>1);
        describeView();gtk_gl_area_queue_render(GTK_GL_AREA(glArea));
    }
    void describeView() {
        std::string text=shownKey+": "+std::to_string(viewer.vertices)+" vertices, "+std::to_string(viewer.triangles)+" triangles, "+std::to_string(viewer.frames.size())+" frames";
        text+=viewer.frame>=0?"\nFrame "+std::to_string(viewer.frame)+": "+viewer.frames[viewer.frame]:std::string("\nRest pose (no animation frames)");
        viewNote(text+"\nDrag to orbit, wheel to zoom, double-click to reset the view.");
    }
    void exportForBlender() {
        if(shown.empty()){message("Select a model in the 3-D viewer first.");return;}
        auto* dialog=gtk_file_chooser_dialog_new("Export model for Blender into a folder",GTK_WINDOW(window),GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,"_Cancel",GTK_RESPONSE_CANCEL,"_Export",GTK_RESPONSE_ACCEPT,nullptr);
        gtk_file_chooser_set_create_folders(GTK_FILE_CHOOSER(dialog),true);
        std::string dir;if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT){gchar* path=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));dir=path?path:"";g_free(path);}
        gtk_widget_destroy(dialog);if(dir.empty())return;
        try{
            std::vector<fs::path> files;std::vector<std::string> clash;
            for(const auto& e:fs::directory_iterator(shown))if(e.is_regular_file()){files.push_back(e.path());if(fs::exists(fs::path(dir)/e.path().filename()))clash.push_back(e.path().filename().string());}
            if(!clash.empty()&&!confirm("Overwrite files in "+dir+"?","These files already exist there:"+bullets(clash),"_Overwrite"))return;
            for(const auto& f:files)fs::copy_file(f,fs::path(dir)/f.filename(),fs::copy_options::overwrite_existing);
            message("Exported "+shownKey+" to "+dir+". In Blender: File ▸ Import ▸ glTF 2.0, open "+fs::path(shownKey).stem().string()+".gltf; animation frames are its shape keys.");
        }catch(const std::exception& e){message(std::string("Export for Blender failed: ")+e.what());}
    }
    void replaceModel() {
        const auto [key,source]=selectedModel();if(key.empty()){message("Select the model to replace in the 3-D viewer list.");return;}
        auto id=selectedMod();
        if(id.empty()){if(!confirm("Replace "+key,"Replacements are stored in a mod and no mod is selected. Create a new mod?","_New mod…")||(id=newMod()).empty())return;}
        auto* dialog=gtk_file_chooser_dialog_new(("Replacement for "+key).c_str(),GTK_WINDOW(window),GTK_FILE_CHOOSER_ACTION_OPEN,"_Cancel",GTK_RESPONSE_CANCEL,"_Choose",GTK_RESPONSE_ACCEPT,nullptr);
        auto* filter=gtk_file_filter_new();gtk_file_filter_set_name(filter,"glTF or A6 model (.gltf, .glb, .mdl)");for(auto* p:{"*.[gG][lL][tT][fF]","*.[gG][lL][bB]","*.[mM][dD][lL]"})gtk_file_filter_add_pattern(filter,p);gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog),filter);
        std::string pick;if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT){gchar* path=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));pick=path?path:"";g_free(path);}
        gtk_widget_destroy(dialog);if(pick.empty())return;
        const auto modDir=modsRoot()/id;ModelPlan plan;
        try{plan=planReplacement(modDir,key,pick);}catch(const std::exception& e){message("Cannot use "+pick+": "+e.what());return;}
        if(plan.copies.empty()){message("That file already is mod "+id+"'s replacement for "+key+".");return;}
        auto rel=[&](const std::vector<fs::path>& paths){std::vector<std::string> r;for(const auto& p:paths)r.push_back(p.lexically_relative(modDir).generic_string());return r;};
        std::vector<fs::path> targets;for(const auto& c:plan.copies)targets.push_back(c.second);
        std::string text="Copy into "+modDir.string()+":"+bullets(rel(targets));
        if(!plan.overwritten.empty())text+="\n\nOverwrites:"+bullets(rel(plan.overwritten));
        if(!plan.removed.empty())text+="\n\nRemoves (one file per model):"+bullets(rel(plan.removed));
        if(!plan.warnings.empty())text+="\n\nWarnings:"+bullets(plan.warnings);
        if(!confirm("Replace "+key+" in mod “"+id+"”?",text,"_Replace"))return;
        text="Replaced "+key+" in mod "+id+". The viewer shows it as the engine loads it; enable the mod and Apply / Play to use it.";
        try{for(const auto& [from,to]:plan.copies){fs::create_directories(to.parent_path());fs::copy_file(from,to,fs::copy_options::overwrite_existing);}for(const auto& p:plan.removed)fs::remove(p);}
        catch(const std::exception& e){text=std::string("Replace model failed: ")+e.what();}
        refreshMods();selectMod(id);refreshModels(key,id);message(text);
    }
    void removeReplacement() {
        const auto [key,source]=selectedModel();const auto id=selectedMod();
        if(key.empty()||id.empty()){message("Select a mod and one of its models (or an original it replaces) in the 3-D viewer.");return;}
        const auto modDir=modsRoot()/id;const auto files=replacementFiles(modDir,key);
        if(files.empty()){message("Mod "+id+" has no file for "+key+".");return;}
        std::vector<std::string> names;for(const auto& f:files)names.push_back(f.lexically_relative(modDir).generic_string());
        if(!confirm("Remove the replacement of "+key+" from mod “"+id+"”?","These files are deleted:"+bullets(names),"_Remove"))return;
        std::string text="Removed mod "+id+"'s replacement of "+key+".";
        try{for(const auto& f:files){fs::remove(f);std::error_code ec;for(auto p=f.parent_path();p!=modDir/"data"&&fs::is_empty(p,ec)&&!ec;p=p.parent_path())fs::remove(p,ec);}}
        catch(const std::exception& e){text=std::string("Remove replacement failed: ")+e.what();}
        refreshMods();selectMod(id);refreshModels(key,"");message(text);
    }
    GtkWidget* viewerPage() {
        auto* paned=gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);gtk_container_set_border_width(GTK_CONTAINER(paned),8);
        auto* left=gtk_box_new(GTK_ORIENTATION_VERTICAL,6);showOriginals=gtk_check_button_new_with_mnemonic("Show _originals");
        gtk_widget_set_tooltip_text(showOriginals,"Also list every game model. Originals the selected mod replaces are marked.");gtk_box_pack_start(GTK_BOX(left),showOriginals,false,false,0);
        g_signal_connect(showOriginals,"toggled",G_CALLBACK(+[](GtkToggleButton*,gpointer d){static_cast<UI*>(d)->refreshModels();}),this);
        modelStore=gtk_list_store_new(3,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING);modelView=gtk_tree_view_new_with_model(GTK_TREE_MODEL(modelStore));g_object_unref(modelStore);
        gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(modelView),-1,"Model (type to search)",gtk_cell_renderer_text_new(),"text",0,nullptr);gtk_tree_view_set_search_column(GTK_TREE_VIEW(modelView),1);
        g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(modelView)),"changed",G_CALLBACK(+[](GtkTreeSelection*,gpointer d){static_cast<UI*>(d)->showModel();}),this);
        auto* scroll=gtk_scrolled_window_new(nullptr,nullptr);gtk_widget_set_size_request(scroll,280,-1);gtk_container_add(GTK_CONTAINER(scroll),modelView);gtk_box_pack_start(GTK_BOX(left),scroll,true,true,0);
        gtk_paned_pack1(GTK_PANED(paned),left,false,false);
        auto* right=gtk_box_new(GTK_ORIENTATION_VERTICAL,6);
        glArea=gtk_gl_area_new();viewer.area=glArea;gtk_gl_area_set_has_depth_buffer(GTK_GL_AREA(glArea),true);gtk_gl_area_set_required_version(GTK_GL_AREA(glArea),3,2);gtk_widget_set_size_request(glArea,420,380);gtk_widget_set_hexpand(glArea,true);
        gtk_widget_add_events(glArea,GDK_BUTTON_PRESS_MASK|GDK_BUTTON_RELEASE_MASK|GDK_BUTTON1_MOTION_MASK|GDK_SCROLL_MASK|GDK_SMOOTH_SCROLL_MASK);
        g_signal_connect(glArea,"realize",G_CALLBACK(+[](GtkWidget* w,gpointer d){auto& ui=*static_cast<UI*>(d);gtk_gl_area_make_current(GTK_GL_AREA(w));
            if(auto* e=gtk_gl_area_get_error(GTK_GL_AREA(w))){ui.viewNote(std::string("3-D view unavailable: ")+e->message);return;}ui.viewer.realize();}),this);
        g_signal_connect(glArea,"unrealize",G_CALLBACK(+[](GtkWidget* w,gpointer d){gtk_gl_area_make_current(GTK_GL_AREA(w));if(!gtk_gl_area_get_error(GTK_GL_AREA(w)))static_cast<UI*>(d)->viewer.unrealize();}),this);
        g_signal_connect(glArea,"render",G_CALLBACK(+[](GtkGLArea* a,GdkGLContext*,gpointer d)->gboolean{const int s=gtk_widget_get_scale_factor(GTK_WIDGET(a));
            static_cast<UI*>(d)->viewer.render(gtk_widget_get_allocated_width(GTK_WIDGET(a))*s,gtk_widget_get_allocated_height(GTK_WIDGET(a))*s);return TRUE;}),this);
        g_signal_connect(glArea,"button-press-event",G_CALLBACK(+[](GtkWidget* w,GdkEventButton* e,gpointer d)->gboolean{auto& v=static_cast<UI*>(d)->viewer;if(e->button!=1)return FALSE;
            if(e->type==GDK_2BUTTON_PRESS){v.reset();gtk_gl_area_queue_render(GTK_GL_AREA(w));}else{v.drag=true;v.x=e->x;v.y=e->y;}return TRUE;}),this);
        g_signal_connect(glArea,"button-release-event",G_CALLBACK(+[](GtkWidget*,GdkEventButton*,gpointer d)->gboolean{static_cast<UI*>(d)->viewer.drag=false;return TRUE;}),this);
        g_signal_connect(glArea,"motion-notify-event",G_CALLBACK(+[](GtkWidget* w,GdkEventMotion* e,gpointer d)->gboolean{auto& v=static_cast<UI*>(d)->viewer;if(!v.drag)return FALSE;
            v.yaw+=(e->x-v.x)*0.01;v.pitch=std::clamp(v.pitch+(e->y-v.y)*0.01,-1.5,1.5);v.x=e->x;v.y=e->y;gtk_gl_area_queue_render(GTK_GL_AREA(w));return TRUE;}),this);
        g_signal_connect(glArea,"scroll-event",G_CALLBACK(+[](GtkWidget* w,GdkEventScroll* e,gpointer d)->gboolean{auto& v=static_cast<UI*>(d)->viewer;
            const double step=e->direction==GDK_SCROLL_UP?-1:e->direction==GDK_SCROLL_DOWN?1:e->direction==GDK_SCROLL_SMOOTH?e->delta_y:0;
            v.zoom=std::clamp(v.zoom*std::pow(1.15,step),0.05,20.0);gtk_gl_area_queue_render(GTK_GL_AREA(w));return TRUE;}),this);
        gtk_box_pack_start(GTK_BOX(right),glArea,true,true,0);
        frameScale=gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,0,1,1);gtk_scale_set_digits(GTK_SCALE(frameScale),0);gtk_widget_set_sensitive(frameScale,false);gtk_widget_set_tooltip_text(frameScale,"Animation frame (the model's per-frame vertex animation)");
        g_signal_connect(frameScale,"value-changed",G_CALLBACK(+[](GtkRange* r,gpointer d){auto& ui=*static_cast<UI*>(d);if(ui.viewer.frames.empty())return;
            ui.viewer.frame=std::clamp(static_cast<int>(std::lround(gtk_range_get_value(r))),0,static_cast<int>(ui.viewer.frames.size())-1);ui.viewer.dirty=true;ui.describeView();gtk_gl_area_queue_render(GTK_GL_AREA(ui.glArea));}),this);
        gtk_box_pack_start(GTK_BOX(right),frameScale,false,false,0);
        auto* actions=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);gtk_box_pack_start(GTK_BOX(right),actions,false,false,0);
        for(auto* w:{button("Export for _Blender…",this,[](gpointer d){static_cast<UI*>(d)->exportForBlender();}),button("Re_place model…",this,[](gpointer d){static_cast<UI*>(d)->replaceModel();}),
            button("Remove repla_cement",this,[](gpointer d){static_cast<UI*>(d)->removeReplacement();})})gtk_box_pack_start(GTK_BOX(actions),w,false,false,0);
        viewInfo=note("Select a model. The selected mod's models are listed; Show originals adds every game model.");gtk_label_set_selectable(GTK_LABEL(viewInfo),true);gtk_box_pack_start(GTK_BOX(right),viewInfo,false,false,0);
        gtk_paned_pack2(GTK_PANED(paned),right,true,false);return paned;
    }
    // ---- construction ----
    void build() {
        window=gtk_window_new(GTK_WINDOW_TOPLEVEL);gtk_window_set_title(GTK_WINDOW(window),"Kabus 22 — Native Settings");gtk_window_set_default_size(GTK_WINDOW(window),900,780);
        g_signal_connect(window,"delete-event",G_CALLBACK(+[](GtkWidget*,GdkEvent*,gpointer data)->gboolean {auto& ui=*static_cast<UI*>(data);if(ui.job){ui.message("Wait for the running game / setup / update task to finish; its exit will be collected here.");return TRUE;}gtk_main_quit();return FALSE;}),this);
        auto* frame=gtk_box_new(GTK_ORIENTATION_VERTICAL,0);gtk_container_add(GTK_CONTAINER(window),frame);
        GtkWidget *heading=nullptr,*subheading=nullptr;gtk_box_pack_start(GTK_BOX(frame),k22::wizard::header(heading,subheading,headerImage),false,false,0);
        gtk_label_set_text(GTK_LABEL(heading),"Kabus 22 Settings");gtk_label_set_text(GTK_LABEL(subheading),"Version " K22_VERSION "  •  Original game files are never modified  •  No automatic renderer fallback");
        auto* outer=gtk_box_new(GTK_ORIENTATION_VERTICAL,10);gtk_container_set_border_width(GTK_CONTAINER(outer),14);gtk_box_pack_start(GTK_BOX(frame),outer,true,true,0);
        auto* pathGrid=gtk_grid_new();gtk_grid_set_column_spacing(GTK_GRID(pathGrid),8);root=gtk_entry_new();row(pathGrid,0,"_Installation",root);
        gtk_entry_set_text(GTK_ENTRY(root),options.root.c_str());browse=gtk_button_new_with_mnemonic("_Browse…");reload=gtk_button_new_with_mnemonic("_Load / Reload");gtk_grid_attach(GTK_GRID(pathGrid),browse,2,0,1,1);gtk_grid_attach(GTK_GRID(pathGrid),reload,3,0,1,1);gtk_box_pack_start(GTK_BOX(outer),pathGrid,false,false,0);
        g_signal_connect(reload,"clicked",G_CALLBACK(+[](GtkButton*,gpointer data){static_cast<UI*>(data)->loadRoot();}),this);
        g_signal_connect(browse,"clicked",G_CALLBACK(+[](GtkButton*,gpointer data){auto& ui=*static_cast<UI*>(data);auto* dialog=gtk_file_chooser_dialog_new("Choose existing Kabus 22 installation",GTK_WINDOW(ui.window),GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,"_Cancel",GTK_RESPONSE_CANCEL,"_Open",GTK_RESPONSE_ACCEPT,nullptr);if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT){gchar* path=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));gtk_entry_set_text(GTK_ENTRY(ui.root),path);g_free(path);ui.loadRoot();}gtk_widget_destroy(dialog);}),this);
        book=gtk_notebook_new();gtk_notebook_set_scrollable(GTK_NOTEBOOK(book),true);gtk_box_pack_start(GTK_BOX(outer),book,true,true,0);GtkWidget* grid=nullptr;
        launchPage=page(book,"Launch",nativeGrid);grid=nativeGrid;
        mode=combo({{"campaign","Campaign"},{"bonus","Bonus mode"}});renderer=combo({{"opengl","OpenGL 3.3+"},{"vulkan-raster","Vulkan 1.3 — raster"},{"vulkan-rt","Vulkan 1.3 — hardware ray tracing"}});
        display=combo({{"native","Native widescreen"},{"authored","Original authored display"}});windowMode=combo({{"borderless","Desktop borderless / authored fullscreen"},{"fullscreen","Fullscreen"},{"windowed","Windowed"}});
        resolution=gtk_entry_new();gtk_entry_set_placeholder_text(GTK_ENTRY(resolution),"Desktop automatic (or WIDTHxHEIGHT)");
        language=combo({{"tr","Türkçe"},{"en","English"},{"de","Deutsch"},{"saved","Keep saved language / game default"}});audio=combo({{"tr","Turkish"},{"en","English"}});aa=combo({{"0","Off (native minimum 1 sample)"},{"1","Basic (1 requested sample)"},{"2","4 requested samples"},{"3","9 requested samples (hardware negotiated)"}});
        camera=combo({{"original","Original level cameras"},{"first-person","First person (Demir and Ebru, community patch style)"}});
        row(grid,0,"Game _mode",mode);row(grid,1,"Rendering _API",renderer,"Hardware requirements are checked by the real engine. Vulkan RT needs ray-query/acceleration-structure support and 1024 sampler bindings; failure never silently chooses raster. This choice overrides the graphics preset's backend.");
        row(grid,2,"_Display policy",display);row(grid,3,"_Window mode",windowMode);row(grid,4,"Native _resolution",resolution,"Original campaign only: minimum 1280x720, maximum 16384x16384. Borderless uses desktop dimensions and ignores this field.");row(grid,5,"Text _language",language);row(grid,6,"Voice / movie language",audio,"Campaign only. German text uses English voices unless Turkish is explicitly selected.");row(grid,7,"Bonus anti-aliasing",aa,"Original compile-time choices; native hardware clamps/negotiates the actual sample count.");
        row(grid,8,"_Camera",camera,"Original campaign only. First person follows the community patch 1.8.5 camera for Demir and Ebru while you control them (mouse turns and aims, left/right button fire/raise weapon); cutscenes, Inzar and frozen scenes keep the original cameras.");
        gtk_widget_set_tooltip_text(language,"Saved patch locale wins on load. Play leaves its byte unchanged unless you intentionally select another language. Spanish, Russian and Polish appear only when the community patch and its language data are installed.");
        gtk_grid_attach(GTK_GRID(grid),note("Patch/custom display stays game-owned. Campaign difficulty belongs to New Game and saves, not this launcher. No speculative sensitivity, software-light or stencil-shadow switches."),0,9,2,1);
        for(auto* w:{mode,display,windowMode})g_signal_connect(w,"changed",G_CALLBACK(+[](GtkComboBox*,gpointer data){auto& ui=*static_cast<UI*>(data);if(ui.settings&&!ui.populating)ui.availability();}),this);
        g_signal_connect(mode,"changed",G_CALLBACK(+[](GtkComboBox*,gpointer data){auto& ui=*static_cast<UI*>(data);if(ui.settings&&!ui.populating)ui.refreshModels();}),this);
        campaignPage=page(book,"Graphics",grid);
        const char* graphics[]={"Authored resolution","Legacy color depth","Lighting quality","Completion buffering","Mipmapping","Anisotropy","Reflections","Blood color","Blood amount","Brightness","Contrast","Gamma"};
        campaign[0]=combo({{"0","640 × 480"},{"1","800 × 600"},{"2","1024 × 768"},{"3","1280 × 1024"}});
        campaign[1]=combo({{"0","16-bit authored compatibility"},{"1","32-bit authored compatibility"}});campaign[2]=combo({{"0","Original quality 0"},{"1","Original quality 1"}});campaign[3]=combo({{"0","Single outstanding completion"},{"1","Two outstanding completions"}});
        for(int i=4;i<=6;++i)campaign[i]=combo({{"0","Off"},{"1","On"}});
        campaign[7]=combo({{"0","Red"},{"1","Green"}});
        campaign[8]=gtk_spin_button_new_with_range(0,100,5);campaign[9]=gtk_spin_button_new_with_range(-100,100,5);campaign[10]=gtk_spin_button_new_with_range(-80,80,5);campaign[11]=gtk_spin_button_new_with_range(-100,100,5);
        for(int i=0;i<12;++i)row(grid,i,graphics[i],campaign[i]);
        gtk_widget_set_tooltip_text(campaign[0],"Authoring compatibility value; native widescreen keeps actual desktop/custom geometry independently.");gtk_widget_set_tooltip_text(campaign[1],"Legacy script/video contract only. Native scene targets stay RGBA8 with depth-stencil.");gtk_widget_set_tooltip_text(campaign[2],"Original LIGHTRES topology choice, not hardware ray tracing.");gtk_widget_set_tooltip_text(campaign[5],"Native anisotropy enabled/disabled; enabled uses hardware-clamped native preference, not a promise of 2×.");gtk_widget_set_tooltip_text(campaign[6],"Controls authored horizontal mirror VIEW admission; not a substitute for Vulkan hardware RT.");
        renderPage=page(book,"Rendering",grid);
        preset=gtk_combo_box_text_new();row(grid,0,"Graphics _preset",preset,"Original = the authored look exactly. Enhanced/Ultra/Custom are engine presets; unsupported features are disabled by the engine and logged. The Launch tab's Rendering API always wins over a preset's backend.");
        backends=note("");gtk_label_set_selectable(GTK_LABEL(backends),true);gtk_grid_attach(GTK_GRID(grid),backends,0,1,2,1);
        auto* customHead=gtk_label_new(nullptr);gtk_label_set_markup(GTK_LABEL(customHead),"<b>Custom preset options</b> (editable when the preset is Custom)");gtk_label_set_xalign(GTK_LABEL(customHead),0);gtk_grid_attach(GTK_GRID(grid),customHead,0,2,2,1);
        customGrid=gtk_grid_new();gtk_grid_set_row_spacing(GTK_GRID(customGrid),9);gtk_grid_set_column_spacing(GTK_GRID(customGrid),18);gtk_grid_attach(GTK_GRID(grid),customGrid,0,3,2,1);
        auto* packHead=gtk_label_new(nullptr);gtk_label_set_markup(GTK_LABEL(packHead),"<b>Texture packs</b> (checked packs load in this order, first wins; none checked = original textures)");gtk_label_set_xalign(GTK_LABEL(packHead),0);gtk_grid_attach(GTK_GRID(grid),packHead,0,4,2,1);
        packs=gtk_list_store_new(4,G_TYPE_BOOLEAN,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_BOOLEAN);packView=gtk_tree_view_new_with_model(GTK_TREE_MODEL(packs));g_object_unref(packs);
        {auto* toggle=gtk_cell_renderer_toggle_new();gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(packView),-1,"Use",toggle,"active",0,"activatable",3,nullptr);
            g_signal_connect(toggle,"toggled",G_CALLBACK(+[](GtkCellRendererToggle*,gchar* path,gpointer data){auto& ui=*static_cast<UI*>(data);GtkTreeIter it;if(gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(ui.packs),&it,path)){gboolean on=false;gtk_tree_model_get(GTK_TREE_MODEL(ui.packs),&it,0,&on,-1);gtk_list_store_set(ui.packs,&it,0,!on,-1);}}),this);
            gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(packView),-1,"Pack",gtk_cell_renderer_text_new(),"text",1,nullptr);gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(packView),-1,"Status",gtk_cell_renderer_text_new(),"text",2,nullptr);}
        auto* packScroll=gtk_scrolled_window_new(nullptr,nullptr);gtk_widget_set_size_request(packScroll,-1,120);gtk_container_add(GTK_CONTAINER(packScroll),packView);gtk_widget_set_hexpand(packScroll,true);gtk_grid_attach(GTK_GRID(grid),packScroll,0,5,2,1);
        auto* packButtons=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);gtk_grid_attach(GTK_GRID(grid),packButtons,0,6,2,1);
        auto move=[](UI& ui,bool up){GtkTreeIter it,other;GtkTreeModel* model=nullptr;if(!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(ui.packView)),&model,&it))return;other=it;if(up?gtk_tree_model_iter_previous(model,&other):gtk_tree_model_iter_next(model,&other))gtk_list_store_swap(ui.packs,&it,&other);};
        packUp=gtk_button_new_with_mnemonic("Move _up");packDown=gtk_button_new_with_mnemonic("Move _down");gtk_box_pack_start(GTK_BOX(packButtons),packUp,false,false,0);gtk_box_pack_start(GTK_BOX(packButtons),packDown,false,false,0);
        g_signal_connect(packUp,"clicked",G_CALLBACK(+[](GtkButton*,gpointer data){decltype(move){}(*static_cast<UI*>(data),true);}),this);g_signal_connect(packDown,"clicked",G_CALLBACK(+[](GtkButton*,gpointer data){decltype(move){}(*static_cast<UI*>(data),false);}),this);
        gtk_grid_attach(GTK_GRID(grid),note("Packs live in ~/.local/share/kabus22/packs/<name>/pack.txt and never replace original files. Ray tracing is the Launch tab's Vulkan RT API; its availability is listed above."),0,7,2,1);
        fpsLimit=gtk_combo_box_text_new();gtk_grid_insert_row(GTK_GRID(grid),1);row(grid,1,"_FPS limit",fpsLimit,"Applies under every preset. Original keeps the game's own cap (campaign 72, Bonus 60). Game speed stays correct at every listed rate. With vsync the display refresh rate still caps higher values; Unlimited turns vsync off and stops at about 321 FPS, the original engine's game-clock floor.");
        for(auto* w:{preset,renderer})g_signal_connect(w,"changed",G_CALLBACK(+[](GtkComboBox*,gpointer data){auto& ui=*static_cast<UI*>(data);if(ui.settings&&!ui.populating)ui.availability();}),this);
        modsPage=page(book,"Mods",grid);
        {auto* head=gtk_label_new(nullptr);gtk_label_set_markup(GTK_LABEL(head),"<b>Mods</b> replace or add game files by name: models, levels, images, sounds, music, movies and campaign scripts. Checked mods load in this order — the <b>first listed wins</b> when two provide the same file. None checked = the original game.");
            gtk_label_set_xalign(GTK_LABEL(head),0);gtk_label_set_line_wrap(GTK_LABEL(head),true);gtk_grid_attach(GTK_GRID(grid),head,0,0,2,1);}
        modStore=gtk_list_store_new(7,G_TYPE_BOOLEAN,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_BOOLEAN,G_TYPE_STRING);modView=gtk_tree_view_new_with_model(GTK_TREE_MODEL(modStore));g_object_unref(modStore);
        {auto* toggle=gtk_cell_renderer_toggle_new();gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(modView),-1,"Use",toggle,"active",0,"activatable",5,nullptr);
            g_signal_connect(toggle,"toggled",G_CALLBACK(+[](GtkCellRendererToggle*,gchar* path,gpointer data){auto& ui=*static_cast<UI*>(data);GtkTreeIter it;if(gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(ui.modStore),&it,path)){gboolean on=false;gtk_tree_model_get(GTK_TREE_MODEL(ui.modStore),&it,0,&on,-1);gtk_list_store_set(ui.modStore,&it,0,!on,-1);ui.describeMod();}}),this);
            const char* titles[]={"Mod (id)","Version","Author","Status"};
            for(int c=1;c<=4;++c){auto* text=gtk_cell_renderer_text_new();g_object_set(text,"ellipsize",PANGO_ELLIPSIZE_END,nullptr);auto* column=gtk_tree_view_column_new_with_attributes(titles[c-1],text,"text",c,nullptr);
                gtk_tree_view_column_set_resizable(column,true);gtk_tree_view_column_set_expand(column,c==1||c==4);gtk_tree_view_append_column(GTK_TREE_VIEW(modView),column);}}
        g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(modView)),"changed",G_CALLBACK(+[](GtkTreeSelection*,gpointer data){auto& ui=*static_cast<UI*>(data);ui.describeMod();ui.refreshModels();}),this);
        {auto* scroll=gtk_scrolled_window_new(nullptr,nullptr);gtk_widget_set_size_request(scroll,-1,140);gtk_container_add(GTK_CONTAINER(scroll),modView);gtk_widget_set_hexpand(scroll,true);gtk_grid_attach(GTK_GRID(grid),scroll,0,1,2,1);}
        auto moveMod=[](UI& ui,bool up){GtkTreeIter it,other;GtkTreeModel* model=nullptr;if(!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(ui.modView)),&model,&it))return;other=it;if(up?gtk_tree_model_iter_previous(model,&other):gtk_tree_model_iter_next(model,&other))gtk_list_store_swap(ui.modStore,&it,&other);ui.describeMod();};
        auto* modButtons=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);gtk_grid_attach(GTK_GRID(grid),modButtons,0,2,2,1);
        auto* modUp=button("▲ _Higher priority",this,[](gpointer d){decltype(moveMod){}(*static_cast<UI*>(d),true);});auto* modDown=button("▼ _Lower priority",this,[](gpointer d){decltype(moveMod){}(*static_cast<UI*>(d),false);});
        for(auto* w:{modUp,modDown,button("Re_fresh",this,[](gpointer d){static_cast<UI*>(d)->refreshMods();}),button("_Open folder",this,[](gpointer d){auto& ui=*static_cast<UI*>(d);auto id=ui.selectedMod();ui.openFolder(id.empty()?modsRoot():modsRoot()/id);})})gtk_box_pack_start(GTK_BOX(modButtons),w,false,false,0);
        auto* modActions=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);gtk_grid_attach(GTK_GRID(grid),modActions,0,3,2,1);
        for(auto* w:{button("_New mod…",this,[](gpointer d){static_cast<UI*>(d)->newMod();}),button("_Install from .zip…",this,[](gpointer d){static_cast<UI*>(d)->installZip();}),
            button("_Export game files…",this,[](gpointer d){static_cast<UI*>(d)->exportFiles();}),button("_Remove",this,[](gpointer d){static_cast<UI*>(d)->removeMod();})})gtk_box_pack_start(GTK_BOX(modActions),w,false,false,0);
        {auto* tabs=gtk_notebook_new();gtk_grid_attach(GTK_GRID(grid),tabs,0,4,2,1);auto* details=gtk_box_new(GTK_ORIENTATION_VERTICAL,0);gtk_container_set_border_width(GTK_CONTAINER(details),8);
            modDetails=note("");gtk_label_set_selectable(GTK_LABEL(modDetails),true);gtk_box_pack_start(GTK_BOX(details),modDetails,false,false,0);
            gtk_box_pack_start(GTK_BOX(details),note("A mod is a folder in ~/.local/share/kabus22/mods/<id>/ with mod.txt and data/. Files under data/ use the game's own names (letter case does not matter), e.g. data/demir_2007.mdl. Export game files… writes every original resource with those names into an empty folder so you can edit copies. Changes apply on Apply / Play; the original game files are never modified."),false,false,0);
            gtk_notebook_append_page(GTK_NOTEBOOK(tabs),details,gtk_label_new("Details"));gtk_notebook_append_page(GTK_NOTEBOOK(tabs),viewerPage(),gtk_label_new("3-D viewer"));}
        audioPage=page(book,"Audio",grid);const char* volumes[]={"Effects volume","Music volume","Dialogue volume"};for(int i=12;i<=14;++i){campaign[i]=gtk_spin_button_new_with_range(0,100,5);row(grid,i-12,volumes[i-12],campaign[i]);}
        gtk_grid_attach(GTK_GRID(grid),note("Original scripts floor some gains above zero; 0 is not a guaranteed exact mute. Playback uses the system's current SDL audio output; this launcher does not change system routing."),0,3,2,1);
        keysPage=page(book,"Keyboard",grid);const char* actions[]={"Forward","Backward","Right","Left","Raise weapon","Fire","Run","Menu","Map","Spell"};
        for(int i=15;i<25;++i){campaign[i]=gtk_combo_box_text_new();for(const auto& key:keys()){auto id=std::to_string(key.code);gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(campaign[i]),id.c_str(),key.name);}row(grid,i-15,actions[i-15],campaign[i],"Physical legacy scan code. Escape/Enter are reserved; duplicate gameplay bindings are rejected on Apply.");}
        joyPage=page(book,"Joystick",grid);for(int i=25;i<31;++i){campaign[i]=gtk_spin_button_new_with_range(1,12,1);row(grid,i-25,actions[i-21],campaign[i]);}gtk_grid_attach(GTK_GRID(grid),note("Original joystick buttons 1–12. Duplicate button assignments are allowed by the original game. Direction axes are not rebindable."),0,6,2,1);
        padPage=page(book,"Controller",grid);
        padMode=combo({{"original","Original — raw joystick exactly as the 2006 game"},{"modern","Modern — SDL game controller mapping"}});row(grid,0,"Controller _mode",padMode);
        deadzone=gtk_spin_button_new_with_range(0,0.9,0.05);gtk_spin_button_set_digits(GTK_SPIN_BUTTON(deadzone),2);row(grid,1,"Stick deadzone (radial)",deadzone);
        sensitivity=gtk_spin_button_new_with_range(0.25,4,0.05);gtk_spin_button_set_digits(GTK_SPIN_BUTTON(sensitivity),2);row(grid,2,"Look / turn sensitivity",sensitivity);
        vibration=gtk_check_button_new_with_label("Vibration");gtk_grid_attach(GTK_GRID(grid),vibration,1,3,1,1);
        gtk_grid_attach(GTK_GRID(grid),note("Button remap: each controller button presses an original joystick button number (the numbers on the Joystick tab: by default 1 raise weapon, 2 fire, 3 run, 4 menu, 5 map, 6 spell). 0 = unmapped. In both modes the D-pad also acts as the arrow keys; in modern mode A also acts as Enter and B as Escape (in original mode they stay joystick buttons 1 and 2). Mapping a D-pad direction additionally presses that joystick button."),0,4,2,1);
        if(std::size(padLabels)!=controllerButtons().size())throw std::runtime_error("Controller label table mismatch");
        for(std::size_t i=0;i<controllerButtons().size();++i){auto* spin=gtk_spin_button_new_with_range(0,12,1);padButtons.push_back(spin);row(grid,5+static_cast<int>(i),padLabels[i],spin);}
        g_signal_connect(padMode,"changed",G_CALLBACK(+[](GtkComboBox*,gpointer data){auto& ui=*static_cast<UI*>(data);if(ui.settings&&!ui.populating)ui.availability();}),this);
        bonusPage=page(book,"Bonus",grid);bonus[0]=combo({{"0","640 × 480"},{"1","800 × 600"},{"2","1024 × 768"},{"3","1280 × 1024"}});bonus[1]=combo({{"0","Off"},{"1","Basic"},{"2","Full"}});bonus[2]=combo({{"0","Off"},{"1","On"}});bonus[3]=combo({{"0","Disabled"},{"1","Enabled"}});bonus[4]=gtk_spin_button_new_with_range(-2097102,2097151,1);
        row(grid,0,"Authored resolution",bonus[0],"Bonus startup requests authored fullscreen at 32-bit compatibility depth.");row(grid,1,"Shader level",bonus[1]);row(grid,2,"Anisotropy",bonus[2]);row(grid,3,"Music",bonus[3]);row(grid,4,"Brightness (stored value)",bonus[4],"50 is neutral; source applies value minus 50. Exact Windows launcher's UI bounds were not recovered; native fixed-point representable values are accepted.");
        bonusLanguage=combo({{"tr","Türkçe"},{"en","English"},{"de","Deutsch"}});row(grid,5,"Bonus language",bonusLanguage);
        gtk_grid_attach(GTK_GRID(grid),note("Bonus anti-aliasing is on Launch. No independent Bonus voice-language consumer or supported online-score mode exists. These are the five actual source-consumed Bonus fields."),0,6,2,1);
        page(book,"Installation",grid);
        installInfo=note("");gtk_label_set_selectable(GTK_LABEL(installInfo),true);gtk_grid_attach(GTK_GRID(grid),installInfo,0,0,3,1);
        source=gtk_entry_new();gtk_entry_set_placeholder_text(GTK_ENTRY(source),"Original ISO, setup.exe (with setup-1.bin) or installed Windows game folder");row(grid,1,"Original _media",source);
        g_signal_connect(source,"changed",G_CALLBACK(+[](GtkEditable*,gpointer data){static_cast<UI*>(data)->availability();}),this);
        auto* choosers=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);gtk_grid_attach(GTK_GRID(grid),choosers,1,2,1,1);
        chooseFile=button("ISO / setup._exe…",this,[](gpointer d){static_cast<UI*>(d)->chooseSource(false);});chooseFolder=button("Windows game _folder…",this,[](gpointer d){static_cast<UI*>(d)->chooseSource(true);});
        gtk_box_pack_start(GTK_BOX(choosers),chooseFile,false,false,0);gtk_box_pack_start(GTK_BOX(choosers),chooseFolder,false,false,0);
        auto* actionsBox=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);gtk_grid_attach(GTK_GRID(grid),actionsBox,0,3,3,1);
        install=button("_Install game / Import Windows game…",this,[](gpointer d){static_cast<UI*>(d)->installGame();});
        verify=button("_Verify assets",this,[](gpointer d){static_cast<UI*>(d)->verifyAssets();});repair=button("Re_pair installation",this,[](gpointer d){static_cast<UI*>(d)->repairInstall();});
        for(auto* w:{install,verify,repair})gtk_box_pack_start(GTK_BOX(actionsBox),w,false,false,0);
        installResult=note("Install / Import opens the graphical installer (preselecting the media above when set). Verify compares installed files with the install inventory. Repair restores only missing or modified game files from the media above; saves, settings and program versions are untouched.");
        gtk_label_set_selectable(GTK_LABEL(installResult),true);gtk_grid_attach(GTK_GRID(grid),installResult,0,4,3,1);
        page(book,"Updates",grid);
        versions=note("");gtk_label_set_selectable(GTK_LABEL(versions),true);gtk_grid_attach(GTK_GRID(grid),versions,0,0,2,1);
        repository=gtk_entry_new();row(grid,1,"GitHub _repository",repository,"OWNER/NAME. Releases must carry a manifest signed with the project's Ed25519 key; unsigned or mismatching payloads are rejected.");
        channel=combo({{"stable","Stable releases"},{"experimental","Experimental (includes pre-releases)"}});row(grid,2,"_Channel",channel);
        auto* updateButtons=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);gtk_grid_attach(GTK_GRID(grid),updateButtons,0,3,2,1);
        checkUpdates=button("_Check for updates",this,[](gpointer d){static_cast<UI*>(d)->check();});applyUpdate=button("Apply _update",this,[](gpointer d){static_cast<UI*>(d)->updateAction(false);});rollback=button("Roll _back",this,[](gpointer d){static_cast<UI*>(d)->updateAction(true);});
        for(auto* w:{checkUpdates,applyUpdate,rollback})gtk_box_pack_start(GTK_BOX(updateButtons),w,false,false,0);
        latestInfo=note("Updates replace the engine, launcher, updater and tools together; original game files, saves and settings are never touched.");gtk_label_set_selectable(GTK_LABEL(latestInfo),true);gtk_grid_attach(GTK_GRID(grid),latestInfo,0,4,2,1);
        gtk_grid_attach(GTK_GRID(grid),textView(notes,220),0,5,2,1);
        logsPage=page(book,"Logs",grid);
        auto* logButtons=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);gtk_grid_attach(GTK_GRID(grid),logButtons,0,0,1,1);
        gtk_box_pack_start(GTK_BOX(logButtons),button("_Refresh",this,[](gpointer d){static_cast<UI*>(d)->refreshLogs();}),false,false,0);
        gtk_box_pack_start(GTK_BOX(logButtons),button("_Open folder",this,[](gpointer d){static_cast<UI*>(d)->openFolder(logDirectory());}),false,false,0);
        gtk_grid_attach(GTK_GRID(grid),note("Engine, setup and updater runs started here are logged privately (newest ten of each kind kept)."),0,1,1,1);
        auto* listScroll=gtk_scrolled_window_new(nullptr,nullptr);gtk_widget_set_size_request(listScroll,-1,150);gtk_widget_set_hexpand(listScroll,true);logList=gtk_list_box_new();gtk_container_add(GTK_CONTAINER(listScroll),logList);gtk_grid_attach(GTK_GRID(grid),listScroll,0,2,1,1);
        g_signal_connect(logList,"row-selected",G_CALLBACK(+[](GtkListBox*,GtkListBoxRow* r,gpointer data){if(r)static_cast<UI*>(data)->showLog(gtk_list_box_row_get_index(r));}),this);
        gtk_grid_attach(GTK_GRID(grid),textView(logText,300),0,3,1,1);
        g_signal_connect(book,"switch-page",G_CALLBACK(+[](GtkNotebook*,GtkWidget* p,guint,gpointer data){auto& ui=*static_cast<UI*>(data);if(p==ui.logsPage)ui.refreshLogs();}),this);
        auto* output=gtk_expander_new("Game output / startup errors");gtk_container_add(GTK_CONTAINER(output),textView(logView,105));gtk_box_pack_start(GTK_BOX(outer),output,false,false,0);
        progress=gtk_progress_bar_new();gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(progress),true);gtk_box_pack_start(GTK_BOX(outer),progress,false,false,0);
        status=note("");gtk_label_set_selectable(GTK_LABEL(status),true);gtk_box_pack_start(GTK_BOX(outer),status,false,false,0);
        auto* buttons=k22::wizard::styled(gtk_box_new(GTK_ORIENTATION_HORIZONTAL,10),"wizard-nav");gtk_container_set_border_width(GTK_CONTAINER(buttons),12);restore=gtk_button_new_with_mnemonic("Restore game _defaults");apply=gtk_button_new_with_mnemonic("_Apply");play=gtk_button_new_with_mnemonic("_Play");gtk_box_pack_start(GTK_BOX(buttons),restore,false,false,0);gtk_box_pack_end(GTK_BOX(buttons),play,false,false,0);gtk_box_pack_end(GTK_BOX(buttons),apply,false,false,0);
        gtk_box_pack_start(GTK_BOX(frame),gtk_separator_new(GTK_ORIENTATION_HORIZONTAL),false,false,0);gtk_box_pack_start(GTK_BOX(frame),buttons,false,false,0);
        g_signal_connect(restore,"clicked",G_CALLBACK(+[](GtkButton*,gpointer data){static_cast<UI*>(data)->defaults();}),this);g_signal_connect(apply,"clicked",G_CALLBACK(+[](GtkButton*,gpointer data){static_cast<UI*>(data)->commit(false);}),this);g_signal_connect(play,"clicked",G_CALLBACK(+[](GtkButton*,gpointer data){static_cast<UI*>(data)->commit(true);}),this);
        gtk_widget_show_all(window);gtk_widget_hide(progress);loadRoot();
    }
};
}
int main(int argc,char** argv) {
    try {
        auto options=arguments(argc,argv);if(options.help){help();return 0;}if(options.version){std::cout<<Version<<'\n';return 0;}if(options.check){selfCheck();std::cout<<"Launcher settings self-check passed\n";return 0;}
        const auto executable=fs::read_symlink("/proc/self/exe");
        if(options.root.empty()){auto found=k22::layout::find_root(executable);options.root=found.empty()?defaultRoot():found;}
        const auto schemas=executable.parent_path().parent_path()/"share/glib-2.0/schemas";
        if(fs::is_regular_file(schemas/"gschemas.compiled")){g_setenv("GSETTINGS_SCHEMA_DIR",schemas.c_str(),true);g_unsetenv("GTK_MODULES");g_unsetenv("GTK3_MODULES");}
        if(!gtk_init_check(&argc,&argv))throw std::runtime_error("Cannot open GTK display; --self-check works without a GUI");
        k22::wizard::style();
        UI ui;ui.options=std::move(options);ui.build();gtk_main();return 0;
    }catch(const std::exception& error){std::cerr<<"kabus22-launcher: "<<error.what()<<'\n';return 1;}
}
