// 文件说明：MinGW 工具链兼容性冒烟测试（纯 C，无数据库依赖）
#include <uORM/abi/uorm_c.h>
#include <stdio.h>

int main(void) {
    printf("version=%s\n", uorm_version());
    printf("drivers=%d\n", uorm_driver_count());
    printf("smoke ok\n");
    return 0;
}
