/*
Copyright(c) 2016-2022 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#pragma once

//= INCLUDES ===========
#include "Definitions.h"
#include <functional>
//======================

namespace Spartan
{
    using Task = std::function<void()>;

    class SP_CLASS ThreadPool
    {
    public:
        static void Initialize();
        static void Shutdown();

        // Add a task.
        static void AddTask(Task&& task);

        // Adds multiple tasks to spread execution of a given function across all available threads.
        static void ParallelLoop(std::function<void(uint32_t work_index_start, uint32_t work_index_end)>&& function, uint32_t work_count);

        // Wait for all threads to finish work
        static void Flush(bool remove_queued = false);

        // Stats
        static uint32_t GetThreadCount();
        static uint32_t GetSupportedThreadCount();
        static uint32_t GetWorkingThreadCount();
        static uint32_t GetIdleThreadCount();
        static bool AreTasksRunning();
    };
}
