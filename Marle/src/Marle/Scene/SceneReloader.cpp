#include "mrlpch.h"
#include "SceneReloader.h"

namespace Marle {

    void CarryOverState(const ComponentRegistry& registry, const Scene& baseline, const Scene& running, Scene& fresh)
    {
        auto carry = [&](Entity before, Entity now, Entity next) {
            if (!before || !now || !next) return;
            for (const std::string& component : registry.GetComponentNames(next)) {
                Json inBaseline = registry.Write(before, component);
                Json inRunning = registry.Write(now, component);
                if (inBaseline.is_null() || inRunning.is_null()) continue;      // not in all three
                Json inFresh = registry.Write(next, component);
                if (inBaseline != inFresh || inRunning == inFresh) continue;    // the file changed it, or nothing to carry

                registry.Remove(next, component);
                if (!registry.Read(next, component, inRunning).empty())          // no longer valid: the file's value stays
                    registry.Read(next, component, inFresh);
            }
        };

        carry(baseline.GetSceneEntity(), running.GetSceneEntity(), fresh.GetSceneEntity());
        for (const Entity& entity : fresh.GetEntities()) {
            const std::string name = entity.GetName();
            carry(baseline.FindEntity(name), running.FindEntity(name), entity);
        }

        if (registry.Write(baseline.GetSceneEntity(), "Random") == registry.Write(fresh.GetSceneEntity(), "Random"))
            fresh.GetRandom().SetState(running.GetRandom().GetState());
    }

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

        m_SceneHash = result.TextHash;
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
            m_SceneHash = result.TextHash;
            m_Status.Loads++;
            reload.Scene = std::move(result.Scene);
        } else {
            m_Status.Failures++;
        }
        return reload;
    }

}
