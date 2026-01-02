#include "ServerLogSink.h"

#include <spdlog/spdlog.h>

namespace
{
constexpr size_t kMaxLogLines = 2000;

bool HasSink(const std::shared_ptr<spdlog::logger>& logger, const std::shared_ptr<spdlog::sinks::sink>& sink)
{
    if (!logger || !sink)
        return false;

    for (const auto& existing : logger->sinks())
    {
        if (existing.get() == sink.get())
            return true;
    }
    return false;
}
} // namespace

ServerLogSink::ServerLogSink(size_t aMaxLines)
    : m_maxLines(aMaxLines)
{
    set_level(spdlog::level::trace);
    set_pattern("%v");
}

void ServerLogSink::ConsumeLines(TiltedPhoques::Vector<TiltedPhoques::String>& aOut)
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (; m_readIndex < m_lines.size(); ++m_readIndex)
        aOut.push_back(m_lines[m_readIndex]);
}

void ServerLogSink::PushExternalLine(const char* aLine)
{
    if (!aLine || aLine[0] == '\0')
        return;

    std::lock_guard<std::mutex> lock(mutex_);
    m_lines.emplace_back(aLine);
    if (m_lines.size() > m_maxLines)
    {
        const size_t trim = m_lines.size() - m_maxLines;
        m_lines.erase(m_lines.begin(), m_lines.begin() + trim);
        if (m_readIndex >= trim)
            m_readIndex -= trim;
        else
            m_readIndex = 0;
    }
}

void ServerLogSink::sink_it_(const spdlog::details::log_msg& msg)
{
    spdlog::memory_buf_t formatted;
    formatter_->format(msg, formatted);

    TiltedPhoques::String line = TiltedPhoques::String(fmt::to_string(formatted).c_str());

    std::lock_guard<std::mutex> lock(mutex_);
    m_lines.emplace_back(std::move(line));
    if (m_lines.size() > m_maxLines)
    {
        const size_t trim = m_lines.size() - m_maxLines;
        m_lines.erase(m_lines.begin(), m_lines.begin() + trim);
        if (m_readIndex >= trim)
            m_readIndex -= trim;
        else
            m_readIndex = 0;
    }
}

std::shared_ptr<ServerLogSink> GetServerLogSink()
{
    static auto sink = std::make_shared<ServerLogSink>(kMaxLogLines);
    return sink;
}

void AttachServerLogSinkToLogger(const std::shared_ptr<spdlog::logger>& aLogger)
{
    const auto sink = GetServerLogSink();
    if (!aLogger || !sink)
        return;

    if (HasSink(aLogger, sink))
        return;

    aLogger->set_level(spdlog::level::trace);
    aLogger->flush_on(spdlog::level::trace);
    aLogger->sinks().push_back(sink);
}

void AttachServerLogSinkToAllLoggers()
{
    spdlog::apply_all([](const std::shared_ptr<spdlog::logger>& logger)
    {
        AttachServerLogSinkToLogger(logger);
    });
}
