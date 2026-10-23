#include "mrlpch.h"
#include "SceneReloader.h"

namespace Marle {

    SceneReloader::SceneReloader(std::string file, const ComponentRegistry& registry)
        : m_Registry(registry)
    {
        m_Status.File = std::move(file);
    }

    SceneReloader::Stamp SceneReloader::ReadStamp() const
    {
        Stamp stamp;
        std::error_code ec;
        stamp.Modified = std::filesystem::last_write_time(m_Status.File, ec);
        if (ec) return stamp;
        stamp.Size = std::filesystem::file_size(m_Status.File, ec);
        stamp.Exists = !ec;
        return stamp;
    }

    SceneLoadResult SceneReloader::Load()
    {
        // The stamp is taken before reading, so a write that lands while we read is seen as a change next time
        m_Stamp = ReadStamp();
        SceneLoadResult result = LoadScene(m_Status.File, m_Registry);

        m_Status.Missing = false;
        m_Status.KeptPrevious = false;
        m_Status.LastOk = result.Ok();
        m_Status.Problems = result.Problems;
        if (result.Scene) m_Status.Loads++;
        return result;
    }

    SceneReload SceneReloader::Poll(bool force)
    {
        SceneReload reload;

        Stamp stamp = ReadStamp();
        if (!stamp.Exists) {
            m_Status.Missing = true;    // e.g. an editor saving through a rename: wait for the file to come back
            return reload;
        }
        m_Status.Missing = false;
        if (!force && stamp == m_Stamp) return reload;

        m_Stamp = stamp;
        reload.Attempted = true;

        SceneLoadResult result = LoadScene(m_Status.File, m_Registry);
        m_Status.LastOk = result.Ok();
        m_Status.Problems = result.Problems;

        m_Status.KeptPrevious = !result.Ok();
        if (result.Ok()) {
            m_Status.Loads++;
            reload.Scene = std::move(result.Scene);
        } else {
            m_Status.Failures++;
        }
        return reload;
    }

}
