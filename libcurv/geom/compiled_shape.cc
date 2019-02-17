// Copyright 2016-2018 Doug Moen
// Licensed under the Apache Licence, version 2.0
// See https://www.apache.org/licenses/LICENSE-2.0

#include <libcurv/geom/compiled_shape.h>
#include <libcurv/geom/tempfile.h>

#include <libcurv/context.h>
#include <libcurv/exception.h>
#include <libcurv/function.h>
#include <libcurv/system.h>

extern "C" {
#include <dlfcn.h>
}
#include <cstdlib>
#include <fstream>

namespace curv { namespace geom {

Compiled_Shape::Compiled_Shape(Shape_Program& rshape)
{
    is_2d_ = rshape.is_2d_;
    is_3d_ = rshape.is_3d_;
    bbox_ = rshape.bbox_;

    At_System cx{rshape.system_};

    // convert shape to C++ source code
    auto cppname = register_tempfile(".cpp");
    std::ofstream cpp(cppname.c_str());
    export_cpp(rshape, cpp);
    cpp.close();

    // compile C++ to optimized object code
    auto cc_cmd = stringify("c++ -fpic -O3 -c ", cppname.c_str());
    //auto cc_cmd = stringify("c++ -fpic -c -g ", cppname.c_str());
    if (system(cc_cmd->c_str()) != 0)
        throw Exception(cx, "c++ compile failed");

    // create shared object
    auto obj_name = register_tempfile(".o");
    auto so_name = register_tempfile(".so");
    auto link_cmd = stringify("c++ -shared -o ", so_name.c_str(), " ", obj_name.c_str());
    if (system(link_cmd->c_str()) != 0)
        throw Exception(cx, "c++ link failed");

    // load shared object
    // TODO: so_name should contain a / character to prevent PATH search.
    // On macOS with a code-signed curv executable, so_name may need to be an
    // absolute pathname.
    void* dll = dlopen(so_name.c_str(), RTLD_NOW|RTLD_LOCAL);
    if (dll == nullptr)
        throw Exception(cx,
            stringify("can't load shared object: ", dlerror()));

    dlerror(); // Clear previous error.
    void* dist_p = dlsym(dll, "dist");
    const char* dist_err = dlerror();
    if (dist_err != nullptr)
        throw Exception(cx,
            stringify("can't load dist function: ",dist_err));
    assert(dist_p != nullptr);
    dist_ = reinterpret_cast<void (*)(const glm::vec4*,float*)>(dist_p);

    void* colour_p = dlsym(dll, "colour");
    const char* colour_err = dlerror();
    if (colour_err != nullptr)
        throw Exception(cx,
            stringify("can't load colour function: ",colour_err));
    assert(colour_p != nullptr);
    colour_ = (void (*)(const glm::vec4*,glm::vec3*))colour_p;
}

void
export_cpp(Shape_Program& shape, std::ostream& out)
{
    GL_Compiler gl(out, GL_Target::cpp, shape.system());
    At_Program cx(shape);

    out <<
        "#include <glm/vec2.hpp>\n"
        "#include <glm/vec3.hpp>\n"
        "#include <glm/vec4.hpp>\n"
        "#include <glm/common.hpp>\n"
        "#include <glm/geometric.hpp>\n"
        "#include <glm/trigonometric.hpp>\n"
        "#include <glm/exponential.hpp>\n"
        "\n"
        "using namespace glm;\n"
        "\n";
    gl.define_function("dist", GL_Type::Vec(4), GL_Type::Num(),
        shape.dist_fun_, cx);
    gl.define_function("colour", GL_Type::Vec(4), GL_Type::Vec(3),
        shape.colour_fun_, cx);
}

}} // namespace
