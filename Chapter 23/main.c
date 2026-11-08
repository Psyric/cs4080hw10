#include <stdio.h>

#include "value.h"
#include "vm.h"

int main(int argc, char** argv) {
    initVM();

    const char* source = "1 + 2 * 3";
    if (argc > 1) {
        source = argv[1];
    }

    Value result = NIL_VAL;
    InterpretResult status = interpretSource(source, &result);
    if (status != INTERPRET_OK) {
        freeVM();
        return 1;
    }

    printf("Result: ");
    printValue(result);
    printf("\n");

    freeVM();
    return 0;
}