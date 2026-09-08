p = 'examples/webconsole.cpp'
s = open(p, encoding='utf-8').read()

# 1) 移除误加的 current 辅助，替换为角色级别工具
old_current = '''    // ---------------- 会话 ----------------
    auto current = [&]() -> std::optional<web::Session> {
        return sessions.validate(req_header_token_);
    };

    // ---------------- 驱动 ----------------'''
new_current = '''    // ---------------- 驱动 ----------------'''
assert old_current in s, 'current helper missing'
s = s.replace(old_current, new_current, 1)

# 2) 匿名命名空间加角色级别工具（放在 parseSqlBody 之后）
old_ns_end = '''} // namespace

int main(int argc, char** argv) {
    unsigned short port = 8080;
    std::string staticDir = "webapp/dist";
    std::string token = genToken();
    std::string storePath = "connections.json";
    std::string usersPath = "users.json";
    std::string adminPassword;   // 为空 = 自动生成并打印
'''
new_ns_end = '''// 角色级别：user=1 < admin=2 < superadmin=3
int roleLevel(const std::string& role) {
    if (role == "superadmin") return 3;
    if (role == "admin") return 2;
    if (role == "user") return 1;
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    unsigned short port = 8080;
    std::string staticDir = "webapp/dist";
    std::string storePath = "connections.json";
    std::string usersPath = "users.json";
    std::string adminPassword;   // 为空 = 自动生成并打印
'''
assert old_ns_end in s, 'ns end missing'
s = s.replace(old_ns_end, new_ns_end, 1)
open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('ok')
PYEOF