#include <iostream>
#include <deque>
#include <algorithm>
#include <numeric>
#include <functional>
#include <toml++/toml.h>

constexpr std::string_view lib_base_name = "t2c";

constexpr std::string_view s_table = "struct {\n";
constexpr std::string_view s_type_int = "int64_t ";
constexpr std::string_view s_type_double = "double ";
constexpr std::string_view s_type_bool = "bool ";
constexpr std::string_view s_type_string = "char* ";
constexpr std::string_view s_type_array = "void** ";
constexpr std::string_view s_type_array_of_int = "int64_t* ";
constexpr std::string_view s_type_array_of_double = "double* ";
constexpr std::string_view s_type_array_of_bool = "bool* ";
constexpr std::string_view s_type_array_of_string = "char** ";

static const std::string cvar(const std::string& var) {
    std::string s = var;
    std::replace( s.begin(), s.end(), '-', '_');
    return s;
}

static const std::string tvar(const std::string& var) {
    std::string s = var;
    std::replace( s.begin(), s.end(), '_', '-');
    return s;
}

struct Field {
    std::string name;
    enum class Type {
        t_int,
        t_double,
        t_bool,
        t_string,
        t_array,
        t_array_of_int,
        t_array_of_double,
        t_array_of_bool,
        t_array_of_string
    } type;

    Field(std::string name, Field::Type type) : 
        type(type) {
            this->name = cvar(name);
        }
};

struct Table {
    std::vector<Field> fields;
    std::vector<Table*> children;
    Table* parent;
    uint8_t depth;
    std::string name;
};

class Reader {
    public:
        int parser(const std::string& file);
        const Table& get_root();

    private:
        std::deque<Table> tables;
        void tabler(Table& parent, const toml::table* table);
        int c_depth;
};

struct Writer {
    public:
        void write(const std::string& name, const Table& root);

    private:
        int mk_indent(int depth) {
            int is = out.size();
            for (int i = 0; i < depth; ++i) {
                out +=  "    ";
            }
            return out.size() - is;
        }
        std::string o_name;
        std::string out;
    
        void h_header();
        void h_struct(const Table& t);
        void h_functions(const std::string& name);
        void h_finalize();

        void c_src(const Table& root);
        void c_finalize();
};

int Reader::parser(const std::string& file) {
    toml::table tbl;
    try {
        tbl = toml::parse_file(file);
    } catch (const toml::parse_error& err) {
        std::cerr << "TOML file " << file << " parsing failed:\n" << err << "\n";
        return 1;
    }

    Table t;
    // Filename to struct name
    std::string s_name = file;
    // Remove path
    size_t pdir = file.find_last_of("\\/");
    if (pdir != std::string::npos) {
        s_name = s_name.substr(pdir+1);
    }
    // Remove extension
    size_t pext = s_name.find(".toml");
    if (pext != std::string::npos) {
        s_name = s_name.substr(0, pext);
    }

    this->c_depth = 0;
    s_name = cvar(s_name);
    s_name += "_t";

    t.parent = NULL;
    t.name = s_name;
    t.depth = this->c_depth;
    this->tables.emplace_back( std::move(t) );

    this->c_depth = 1;
    this->tabler(this->tables.back(), &tbl); 

    return 0;
}

const Table& Reader::get_root() {
    return this->tables[0];
}

void Reader::tabler(Table& parent, const toml::table* table) {
    table->for_each([this, &parent](auto& key, auto& value) {
        Table t;
        bool mixed_array = false;
        switch (value.type()) {
            case toml::node_type::table:
                t.depth = this->c_depth;
                t.name = key.data();
                t.parent = &parent;
                ++this->c_depth;
                this->tables.emplace_back(std::move(t));
                parent.children.emplace_back(&this->tables.back());

                this->tabler(this->tables.back(), value.as_table());
                --this->c_depth;
                break;

            case toml::node_type::integer:
                parent.fields.emplace_back(Field(key.data(), Field::Type::t_int));
                break;

            case toml::node_type::string:
                parent.fields.emplace_back(Field(key.data(), Field::Type::t_string));
                break;

            case toml::node_type::floating_point:
                parent.fields.emplace_back(Field(key.data(), Field::Type::t_double));
                break;

            case toml::node_type::boolean:
                parent.fields.emplace_back(Field(key.data(), Field::Type::t_bool));
                break;

            case toml::node_type::array:
                
                for (const auto& el : *value.as_array()) {
                    if (el.type() != value.as_array()->at(0).type()) {
                        mixed_array = true;
                        break;
                    }
                }
                if (!mixed_array) {
                    switch (value.as_array()->at(0).type()) {
                        case toml::node_type::integer:
                            parent.fields.emplace_back(Field(key.data(), Field::Type::t_array_of_int));
                            break;

                        case toml::node_type::floating_point:
                            parent.fields.emplace_back(Field(key.data(), Field::Type::t_array_of_double));
                            break;

                        case toml::node_type::boolean:
                            parent.fields.emplace_back(Field(key.data(), Field::Type::t_array_of_bool));
                            break;

                        case toml::node_type::string:
                            parent.fields.emplace_back(Field(key.data(), Field::Type::t_array_of_string));
                            break;

                        default:
                            /* parent.fields.emplace_back(Field(key.data(), Field::Type::t_array)); */
                            break;
                    }
                }
                break;

            default:
                break;
        }
    });
}

void Writer::h_header() {
    this->out += R"(#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef )";
}

void Writer::h_struct(const Table& t) {
    this->mk_indent(t.depth);
    this->out += s_table;

    for (const Field& f: t.fields) {
        this->mk_indent(t.depth + 1);
        switch (f.type) {
            case Field::Type::t_int:
                this->out += std::string(s_type_int) + f.name + ";\n";
                break;
            case Field::Type::t_string:
                this->out += std::string(s_type_string) + f.name + ";\n";
                break;

            case Field::Type::t_double:
                this->out += std::string(s_type_double) + f.name + ";\n";
                break;

            case Field::Type::t_bool:
                this->out += std::string(s_type_bool) + f.name + ";\n";
                break;

            case Field::Type::t_array:
                this->out += std::string(s_type_array) + f.name + ";\n";
                this->mk_indent(t.depth + 1);
                this->out += "size_t " + f.name + "_len;\n";
                break;

            case Field::Type::t_array_of_int:
                this->out += std::string(s_type_array_of_int) + f.name + ";\n";
                this->mk_indent(t.depth + 1);
                this->out += "size_t " + f.name + "_len;\n";
                break;

            case Field::Type::t_array_of_double:
                this->out += std::string(s_type_array_of_double) + f.name + ";\n";
                this->mk_indent(t.depth + 1);
                this->out += "size_t " + f.name + "_len;\n";
                break;

            case Field::Type::t_array_of_bool:
                this->out += std::string(s_type_array_of_bool) + f.name + ";\n";
                this->mk_indent(t.depth + 1);
                this->out += "size_t " + f.name + "_len;\n";
                break;

            case Field::Type::t_array_of_string:
                this->out += std::string(s_type_array_of_string) + f.name + ";\n";
                this->mk_indent(t.depth + 1);
                this->out += "size_t " + f.name + "_len;\n";
                break;

            default:
                break;
        }
    }

    for (Table* c: t.children) {
        this->h_struct(*c);
    }

    mk_indent(t.depth);
    this->out += "} " + cvar(t.name) + ";\n";
}

void Writer::h_functions(const std::string& name) {
    const std::string base_name = name.substr(0, name.size()-2);
    this->out += R"(
#ifdef __cplusplus
extern "C" {
#endif
int  )";
    this->out += lib_base_name.size() ? std::string(lib_base_name) + "_" : "";
    this->out += base_name+"_read(const char* file, " +name+ "** "+base_name+");\n";
    this->out += "void "+ (lib_base_name.size() ? std::string(lib_base_name)+"_" : "") + base_name+"_print(const "+name+"* "+base_name+");\n";
    this->out += "void "+ (lib_base_name.size() ? std::string(lib_base_name)+"_" : "") + base_name+"_free("+name+"* "+base_name+");";
    this->out += R"(
#ifdef __cplusplus
}
#endif)";
}

void Writer::h_finalize() {
    std::ofstream header;
    header.open((lib_base_name.size() ? std::string(lib_base_name) + "-" : "") + this->o_name +".h");
    header << out;
    header.close();
}

void Writer::c_src(const Table& root) {
    const std::string& name = root.name;
    const std::string base_name = name.substr(0, name.size()-2);
    this->out += "#include \"" + (lib_base_name.size() ? std::string(lib_base_name) + "-" : "") + this->o_name; 
    this->out += R"(.h"
#include <stdlib.h>
#include <toml.h>

int )";
    this->out += (lib_base_name.size() ? std::string(lib_base_name) + "_" : "");
    this->out += base_name + "_read(const char* file_path, "+name+"** "+base_name+") {";
    this->out += R"(
    FILE* fp;
    toml_table_t* root;
    char errbuf[200];

    if (*)";
    this->out += base_name + " == NULL) {\n";
    this->out += "        *"+base_name+" = calloc(1, sizeof("+name+"));\n";
    this->out += "    }\n";
    
    this->out += R"(
    /* Open the file. */
    if (0 == (fp = fopen(file_path, "r"))) {
        fprintf(stderr, ")";
    this->out += (lib_base_name.size() ? std::string(lib_base_name) + "_" : "") + base_name;
    this->out += R"(_read() failed: couldn't open %s", file_path);
        return 1;
    }

    /* Run the file through the parser. */
    root = toml_parse_file(fp, errbuf, sizeof(errbuf));
    if (0 == root) {
        fprintf(stderr, ")";
    this->out += (lib_base_name.size() ? std::string(lib_base_name) + "_" : "") + base_name;
    this->out += R"(_read() failed: error while parsing %s", file_path);
        return 1;
    }
    fclose(fp);
    // Tables 
    toml_table_t )";

    // Tables
    std::function<std::string(const Table& t, std::string path)> get_path;
    get_path = [&get_path](const Table& t, std::string path)->std::string {
        if (t.parent) {
            if (t.parent->depth != 0) {
                path = t.parent->name + "_" + path;
            } else {
                path = "root_" + path;
            }
            return (get_path(*t.parent, path));
        }
        return cvar(path);
    };
    std::function<std::string(const Table& t, std::string path)> get_parent_path;
    get_parent_path = [&get_parent_path](const Table& t, std::string path)->std::string {
        if (t.parent) {
            if (t.parent->depth != 0) {
                path = t.parent->name + (path.size()? "_" + path : "");
            } else {
                path = "root" + (path.size()? "_" + path : "");
            }
            return (get_parent_path(*t.parent, path));
        }
        return cvar(path);
    };

    std::function<std::string(const Table& t, std::string path)> get_path_var;
    get_path_var = [&get_path_var](const Table& t, std::string path)->std::string {
        if (t.parent) {
            path = t.name + "." + path;
            return (get_path_var(*t.parent, path));
        }
        return cvar(path);
    };

    std::function<void(const Table&)> decl_r;
    decl_r = [&] (const Table& t)->void {
        this->out += "*" + get_path(t, t.name) + ", ";
        for (const Table* c: t.children) {
            decl_r(*c);
        }
    };
    for (const Table* c: root.children) {
        decl_r(*c);
    }
    *(this->out.end()-2) = ';';
    
    this->out += "\n";

    std::function<void(const Table&)> check_r;
    check_r = [&] (const Table& t)->void {
        this->out += "    if (!("+get_path(t,t.name)+" = toml_table_in("+get_parent_path(t,"")+", \""+tvar(t.name)+"\"))) {\n\
        fprintf(stderr, \""+(lib_base_name.size()?std::string(lib_base_name)+"_":"")+
        base_name+"_read() failed: failed locating ["+t.name+"] table\");\n\
        return 1;\n    }\n";
        for (const Table* c: t.children) {
            check_r(*c);
        }
    };
    for (const Table* c: root.children) {
        check_r(*c);
    }

    // Fields
    this->out += "\n    toml_datum_t datum;\n    toml_array_t* arr;\n";
    std::function<void(const Table&)> read_r;
    read_r = [&] (const Table& t)->void {
        for (const Field& f: t.fields) {
            switch (f.type) {
                case Field::Type::t_int:
                    this->out += "    datum = toml_int_in("+get_path(t,t.name)+", \""+tvar(f.name)+"\");\n";
                    this->out += "    (*"+base_name+")->"+get_path_var(t, f.name)+" = datum.u.i;\n";
                    break;
                case Field::Type::t_double:
                    this->out += "    datum = toml_double_in("+get_path(t,t.name)+", \""+tvar(f.name)+"\");\n";
                    this->out += "    (*"+base_name+")->"+get_path_var(t, f.name)+" = datum.u.d;\n";
                    break;
                case Field::Type::t_bool:
                    this->out += "    datum = toml_bool_in("+get_path(t,t.name)+", \""+tvar(f.name)+"\");\n";
                    this->out += "    (*"+base_name+")->"+get_path_var(t, f.name)+" = datum.u.b;\n";
                    break;
                case Field::Type::t_string:
                    this->out += "    datum = toml_string_in("+get_path(t,t.name)+", \""+tvar(f.name)+"\");\n";
                    this->out += "    (*"+base_name+")->"+get_path_var(t, f.name)+" = datum.u.s;\n";
                    break;

                case Field::Type::t_array_of_int:
                    this->out += "    arr = toml_array_in("+get_path(t,t.name)+", \""+tvar(f.name)+"\");\n";
                    this->out += "    (*"+base_name+")->"+get_path_var(t, f.name)+" = malloc(toml_array_nelem(arr) * sizeof(int64_t));\n";
                    this->out += "    for (int i = 0; i < toml_array_nelem(arr); ++i) {\n        datum = toml_int_at(arr, i);\n";
                    this->out += "        (*"+base_name+")->"+get_path_var(t, f.name)+"[i] = datum.u.i;\n";
                    this->out += "    }\n";
                    this->out += "    (*"+base_name+")->"+get_path_var(t, f.name)+"_len = toml_array_nelem(arr);\n";
                    break;

                case Field::Type::t_array_of_double:
                    this->out += "    arr = toml_array_in("+get_path(t,t.name)+", \""+tvar(f.name)+"\");\n";
                    this->out += "    (*"+base_name+")->"+get_path_var(t, f.name)+" = malloc(toml_array_nelem(arr) * sizeof(double));\n";
                    this->out += "    for (int i = 0; i < toml_array_nelem(arr); ++i) {\n        datum = toml_double_at(arr, i);\n";
                    this->out += "        (*"+base_name+")->"+get_path_var(t, f.name)+"[i] = datum.u.d;\n";
                    this->out += "    }\n";
                    this->out += "    (*"+base_name+")->"+get_path_var(t, f.name)+"_len = toml_array_nelem(arr);\n";
                    break;

                case Field::Type::t_array_of_bool:
                    this->out += "    arr = toml_array_in("+get_path(t,t.name)+", \""+tvar(f.name)+"\");\n";
                    this->out += "    (*"+base_name+")->"+get_path_var(t, f.name)+" = malloc(toml_array_nelem(arr) * sizeof(double));\n";
                    this->out += "    for (int i = 0; i < toml_array_nelem(arr); ++i) {\n        datum = toml_bool_at(arr, i);\n";
                    this->out += "        (*"+base_name+")->"+get_path_var(t, f.name)+"[i] = datum.u.b;\n";
                    this->out += "    }\n";
                    this->out +=      "(*"+base_name+")->"+get_path_var(t, f.name)+"_len = toml_array_nelem(arr);\n";
                    break;

                case Field::Type::t_array_of_string:
                    this->out += "    arr = toml_array_in("+get_path(t,t.name)+", \""+tvar(f.name)+"\");\n";
                    this->out += "    (*"+base_name+")->"+get_path_var(t, f.name)+" = malloc(toml_array_nelem(arr) * sizeof(char*));\n";
                    this->out += "    for (int i = 0; i < toml_array_nelem(arr); ++i) {\n        datum = toml_string_at(arr, i);\n";
                    this->out += "        (*"+base_name+")->"+get_path_var(t, f.name)+"[i] = datum.u.s;\n";
                    this->out += "    }\n";
                    this->out += "    (*"+base_name+")->"+get_path_var(t, f.name)+"_len = toml_array_nelem(arr);\n";
                    break;

                default:
                    break;
            }
        }
        for (const Table* c: t.children) {
            read_r(*c);
        }
    };
    read_r(root);

    this->out += "\n    toml_free(root);\n    return 0;\n}\n\n";
    this->out += "void "+(lib_base_name.size() ? std::string(lib_base_name)+"_" :"")+
                 base_name+"_print(const "+name+"* "+base_name+") {\n";
    this->out += "    printf(\"Read "+base_name+".toml values:\\n\");\n\n";

    std::function<void(const Table&)> print_r;
    print_r = [&] (const Table& t)->void {
        for (const Field& f: t.fields) {
            switch (f.type) {
                case Field::Type::t_int:
                    this->out += "    printf(\""+base_name+"."+get_path_var(t,f.name)+" = %ld\\n\", "+base_name+"->"+get_path_var(t,f.name)+");\n";
                    break;

                case Field::Type::t_double:
                    this->out += "    printf(\""+base_name+"."+get_path_var(t,f.name)+" = %lf\\n\", "+base_name+"->"+get_path_var(t,f.name)+");\n";
                    break;

                case Field::Type::t_bool:
                    this->out += "    printf(\""+base_name+"."+get_path_var(t,f.name)+" = %s\\n\", "+base_name+"->"+get_path_var(t,f.name)+"? \"true\":\"false\");\n";
                    break;

                case Field::Type::t_string:
                    this->out += "    printf(\""+base_name+"."+get_path_var(t,f.name)+" = %s\\n\", "+base_name+"->"+get_path_var(t,f.name)+");\n";
                    break;

                case Field::Type::t_array_of_int:
                    this->out += "    for (int i = 0; i < "+base_name+"->"+get_path_var(t, f.name)+"_len; ++i) {\n";
                    this->out += "        printf(\""+base_name+"."+get_path_var(t,f.name)+"[%d] = %ld\\n\", i, "+base_name+"->"+get_path_var(t,f.name)+"[i]);\n";
                    this->out += "    };\n";
                    break;

                case Field::Type::t_array_of_double:
                    this->out += "    for (int i = 0; i < "+base_name+"->"+get_path_var(t, f.name)+"_len; ++i) {\n";
                    this->out += "        printf(\""+base_name+"."+get_path_var(t,f.name)+"[%d] = %lf\\n\", i, "+base_name+"->"+get_path_var(t,f.name)+"[i]);\n";
                    this->out += "    };\n";
                    break;

                case Field::Type::t_array_of_bool:
                    this->out += "    for (int i = 0; i < "+base_name+"->"+get_path_var(t, f.name)+"_len; ++i) {\n";
                    this->out += "        printf(\""+base_name+"."+get_path_var(t,f.name)+"[%d] = %s\\n\", i, "+base_name+"->"+get_path_var(t,f.name)+"[i]?\"true\":\"false\");\n";
                    this->out += "    };\n";
                    break;

                case Field::Type::t_array_of_string:
                    this->out += "    for (int i = 0; i < "+base_name+"->"+get_path_var(t, f.name)+"_len; ++i) {\n";
                    this->out += "        printf(\""+base_name+"."+get_path_var(t,f.name)+"[%d] = %s\\n\", i, "+base_name+"->"+get_path_var(t,f.name)+"[i]);\n";
                    this->out += "    };\n";
                    break;

                default:
                    break;
            }
        }
        for (const Table* c: t.children) {
            print_r(*c);
        }
    };
    print_r(root);

    this->out += "\n    fflush(stdout);\n}\n\n";
    this->out += "    void "+(lib_base_name.size()?std::string(lib_base_name)+"_":"")+base_name+"_free("+name+"* "+base_name+") {\n";

    std::function<void(const Table&)> free_r;
    free_r = [&] (const Table& t)->void {
        for (const Field& f: t.fields) {
            switch (f.type) {
                case Field::Type::t_string:
                case Field::Type::t_array_of_int:
                case Field::Type::t_array_of_bool:
                case Field::Type::t_array_of_double:
                    this->out += "    free("+base_name+"->"+get_path_var(t, f.name)+");\n";
                    break;

                case Field::Type::t_array_of_string:
                    this->out += "    for (int i = 0; i < "+base_name+"->"+get_path_var(t, f.name)+"_len; ++i) {\n";
                    this->out += "        free("+base_name+"->"+get_path_var(t, f.name)+"[i]);\n";
                    this->out += "    }\n";
                    this->out += "    free("+base_name+"->"+get_path_var(t, f.name)+");\n";
                    break;

                default:
                    break;
            }
        }
        for (const Table* c: t.children) {
            free_r(*c);
        }
    };
    free_r(root);

    this->out += "\n    free("+base_name+");\n}\n";
}

void Writer::c_finalize() {
    std::ofstream src;
    src.open((lib_base_name.size()?std::string(lib_base_name)+"-":"")+this->o_name+".c");
    src << out;
    src.close();
}

void Writer::write(const std::string& name, const Table& root) {
    this->o_name = name;
    // Remove path
    size_t pdir = this->o_name.find_last_of("\\/");
    if (pdir != std::string::npos) {
        this->o_name = this->o_name.substr(pdir+1);
    }
    // Remove extension
    size_t pext = this->o_name.find(".toml");
    if (pext != std::string::npos) {
        this->o_name = this->o_name.substr(0, pext);
    }

    this->h_header();
    this->h_struct(root);
    this->h_functions(root.name);
    this->h_finalize();
    this->out.clear();

    this->c_src(root);
    this->c_finalize();
    this->out.clear();
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: %s TFILE.toml", argv[0]);
        exit(1);
    }

    Writer writer;
    Reader reader;

    if (reader.parser(argv[1])) {
        exit(1);
    }

    writer.write(argv[1], reader.get_root());

    return 0;
}
