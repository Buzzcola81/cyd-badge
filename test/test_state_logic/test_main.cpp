#include <unity.h>

#include "app_logic.h"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_project_name_exists()
{
    TEST_ASSERT_TRUE(hasProjectName());
}

int main(int argc, char **argv)
{
    UNITY_BEGIN();
    RUN_TEST(test_project_name_exists);
    return UNITY_END();
}
