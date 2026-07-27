#include <string>
#include <vector>

int RunCli(const std::vector<std::string>& args);

int main(int argc, char** argv)
{
    std::vector<std::string> args;
    args.reserve(static_cast<std::size_t>(argc > 1 ? argc - 1 : 0));

    for (int i = 1; i < argc; ++i)
    {
        args.emplace_back(argv[i]);
    }

    return RunCli(args);
}
