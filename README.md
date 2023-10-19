# TOML2C
Compiles a TOML file into a C struct, generating also helper functions.

## Limitations
Currently does not support array of tables or mixed-type arrays. 

## Build
The TOML2C compiler requires [toml++](https://github.com/marzer/tomlplusplus) installed.

Compile with `g++ toml2c.cpp -o t2c`.
Run `./t2c FILE.toml`, to generate the C code for FILE.toml.

By default the output files and functions will be prefixed with `t2c`.
This can be changed by modifying the variable `lib_base_name` in the source code.

## Usage
The above outputs a `t2c-FILE.h` and a `t2c-FILE.c` which can be used as a library.
To use the compiled C code, [tomlc99](https://github.com/cktan/tomlc99) needs to be installed.

## Example
```TOML
# pet.toml
[cat]
name = "Oliver"
age = 3
weight = 2.124
sleep-cycle = [800, 1600]

[cat.family]
parent = true
children = ["Margot", "Angel"]
```

```C
#include "t2c-pet.h"
#include <stdlib.h>

int main() {
    // NULL ptr or zero-initialized struct
    t2c_pet_t* pet = NULL;

    // Read toml file into pet
    if (t2c_pet_read("pet.toml", &pet)) {
        exit(1);
    }

    // Do stuff
    printf("My cat is named %s!\n", pet->cat.name);

    // Can also print all values read
    t2c_pet_print(pet);

    // Free everything
    t2c_pet_free(pet);

    return 0;
}
```
