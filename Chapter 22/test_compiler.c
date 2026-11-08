#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vm.h"

static void assertExpression(const char* source, double expected) {
    Value result = NUMBER_VAL(0);
    assert(interpretSource(source, &result) == INTERPRET_OK);
    assert(IS_NUMBER(result));
    assert(result.as.number > expected - 0.000001 && result.as.number < expected + 0.000001);
}

static void assertBoolExpression(const char* source, bool expected) {
    Value result = NUMBER_VAL(0);
    assert(interpretSource(source, &result) == INTERPRET_OK);
    assert(IS_BOOL(result));
    assert(AS_BOOL(result) == expected);
}

static void testTypes(void) {
    assertExpression("1 + 2 * 3", 7.0);
    assertExpression("(1 + 2) * 3", 9.0);
    assertExpression("-1 + 2 * 3", 5.0);
    assertExpression("(-1 + 2) * 3 - -4", 7.0);

    assertBoolExpression("true", true);
    assertBoolExpression("false", false);
    assertBoolExpression("!true", false);
    assertBoolExpression("!false", true);
    assertBoolExpression("1 == 1", true);
    assertBoolExpression("1 != 2", true);
    assertBoolExpression("3 > 2", true);
    assertBoolExpression("3 < 2", false);
    assertBoolExpression("nil == nil", true);

    puts("Challenge checks passed: numeric, boolean, nil, and equality behavior all work.");
}

int main(int argc, char** argv) {
    initVM();

    if (argc == 1 || (argc == 2 && strcmp(argv[1], "--all") == 0)) {
        testTypes();
    } else if (argc == 2 && strcmp(argv[1], "--types") == 0) {
        testTypes();
    } else {
        fprintf(stderr, "Usage: %s [--all|--types]\n", argv[0]);
        freeVM();
        return EXIT_FAILURE;
    }

    freeVM();
    return EXIT_SUCCESS;
}
