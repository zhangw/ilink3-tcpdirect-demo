/*
 * test_validation.c — unit tests for validate_fixed_fields
 */
#include "../framework/test_framework.h"
#include "../../ilink3_auth.h"

static ilink3_fields_t make_valid(void)
{
    ilink3_fields_t f;
    memset(&f, 0, sizeof(f));
    memcpy(f.access_key, "ABCDEFGHIJ0123456789", 20); /* exactly 20 chars */
    memcpy(f.session_id, "ABC", 3);
    memcpy(f.firm_id,    "ABCDE", 5);
    strncpy(f.interface,  "eth0", sizeof(f.interface) - 1);
    strncpy(f.app_name,   "demo", sizeof(f.app_name) - 1);
    strncpy(f.app_ver,    "1.0", sizeof(f.app_ver) - 1);
    strncpy(f.app_vendor, "demo", sizeof(f.app_vendor) - 1);
    return f;
}

TEST(test_valid_fields_pass)
{
    ilink3_fields_t f = make_valid();
    ASSERT_OK(validate_fixed_fields(&f));
    return 0;
}

TEST(test_access_key_too_short)
{
    ilink3_fields_t f = make_valid();
    strncpy(f.access_key, "SHORT", sizeof(f.access_key) - 1);
    ASSERT_FAIL(validate_fixed_fields(&f));
    return 0;
}

TEST(test_access_key_empty)
{
    ilink3_fields_t f = make_valid();
    f.access_key[0] = '\0';
    ASSERT_FAIL(validate_fixed_fields(&f));
    return 0;
}

TEST(test_session_empty)
{
    ilink3_fields_t f = make_valid();
    f.session_id[0] = '\0';
    ASSERT_FAIL(validate_fixed_fields(&f));
    return 0;
}

TEST(test_session_1char)
{
    ilink3_fields_t f = make_valid();
    strncpy(f.session_id, "A", sizeof(f.session_id) - 1);
    ASSERT_OK(validate_fixed_fields(&f));
    return 0;
}

TEST(test_session_3chars)
{
    ilink3_fields_t f = make_valid();
    memcpy(f.session_id, "ABC", 3);
    ASSERT_OK(validate_fixed_fields(&f));
    return 0;
}

TEST(test_firm_empty)
{
    ilink3_fields_t f = make_valid();
    f.firm_id[0] = '\0';
    ASSERT_FAIL(validate_fixed_fields(&f));
    return 0;
}

TEST(test_firm_1char)
{
    ilink3_fields_t f = make_valid();
    strncpy(f.firm_id, "A", sizeof(f.firm_id) - 1);
    ASSERT_OK(validate_fixed_fields(&f));
    return 0;
}

TEST(test_firm_5chars)
{
    ilink3_fields_t f = make_valid();
    memcpy(f.firm_id, "ABCDE", 5);
    ASSERT_OK(validate_fixed_fields(&f));
    return 0;
}

TEST(test_interface_empty)
{
    ilink3_fields_t f = make_valid();
    f.interface[0] = '\0';
    ASSERT_FAIL(validate_fixed_fields(&f));
    return 0;
}

TEST(test_app_name_empty)
{
    ilink3_fields_t f = make_valid();
    f.app_name[0] = '\0';
    ASSERT_FAIL(validate_fixed_fields(&f));
    return 0;
}

TEST(test_app_ver_empty)
{
    ilink3_fields_t f = make_valid();
    f.app_ver[0] = '\0';
    ASSERT_FAIL(validate_fixed_fields(&f));
    return 0;
}

TEST(test_app_vendor_empty)
{
    ilink3_fields_t f = make_valid();
    f.app_vendor[0] = '\0';
    ASSERT_FAIL(validate_fixed_fields(&f));
    return 0;
}

int main(int argc, char **argv)
{
    TEST_INIT("test_validation");

    RUN(test_valid_fields_pass);
    RUN(test_access_key_too_short);
    RUN(test_access_key_empty);
    RUN(test_session_empty);
    RUN(test_session_1char);
    RUN(test_session_3chars);
    RUN(test_firm_empty);
    RUN(test_firm_1char);
    RUN(test_firm_5chars);
    RUN(test_interface_empty);
    RUN(test_app_name_empty);
    RUN(test_app_ver_empty);
    RUN(test_app_vendor_empty);

    return TEST_REPORT();
}
