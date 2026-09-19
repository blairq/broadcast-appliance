/* cmake/mbedtls_user_config.h — ajustes de mbedTLS para este proyecto
 * (se agrega al config por defecto via MBEDTLS_USER_CONFIG_FILE).
 *
 * Sin autotests: no se usan, y con ellos mbedTLS cuenta multiplicaciones de
 * curva en un contador global sin proteger (ecp.c, `mul_count`), que es una
 * carrera real entre hilos (la ve TSan en el tunel WireGuard). */
#undef MBEDTLS_SELF_TEST
