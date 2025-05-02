
local execute_command = require('util').execute_command


local private = dofile(vim.fn.expand('%:p:h') .. '/.private.lua')


local cmake_debug = function()
  return execute_command('cmake-debug', [[cmake -DCMAKE_BUILD_TYPE=Debug -DDEBUG=ON -DCMAKE_MAKE_PROGRAM=ninja -G Ninja -S . -B cmake-build-debug-visual-studio -DCMAKE_EXPORT_COMPILE_COMMANDS=ON]])
end

local cmake_release = function()
  vim.env.VELOPACK_UPDATE_ROOT = private.VELOPACK_ROOT
  return execute_command('cmake-release', [[cmake -DCMAKE_BUILD_TYPE=Release -DDEBUG=OFF -DCMAKE_MAKE_PROGRAM=ninja -G Ninja -S . -B cmake-build-release-visual-studio -DCMAKE_EXPORT_COMPILE_COMMANDS=ON]])
end

local cmake_release_online = function()
  vim.env.VELOPACK_UPDATE_ROOT = private.VELOPACK_ROOT_ONLINE
  return execute_command('cmake-release-online', [[cmake -DCMAKE_BUILD_TYPE=Release -DDEBUG=OFF -DCMAKE_MAKE_PROGRAM=ninja -G Ninja -S . -B cmake-build-release-online-visual-studio -DCMAKE_EXPORT_COMPILE_COMMANDS=ON]])
end

local build_debug = function(on_exit)
  return execute_command('build-debug', [[cmake --build cmake-build-debug-visual-studio --config Debug --target recorder]], on_exit)
end

local build_release = function()
  return execute_command('build-release', [[cmake --build cmake-build-release-visual-studio --config Release --target recorder ]])
end

local build_release_online = function()
  return execute_command('build-release-online', [[cmake --build cmake-build-release-online-visual-studio --config Release --target recorder]])
end


vim.g.build_function = function(on_finish)
  build_debug(on_finish)
end


vim.g.debug_config = {
  name = "Recorder debug",
  type = "codelldb",
  request = "launch",
  program = '${workspaceFolder}/cmake-build-debug-visual-studio/recorder.exe',
  cwd = '${workspaceFolder}/workdir',
  stopOnEntry = false,
  args = {},
}

require('dap').configurations.cpp = { vim.g.debug_config }

vim.api.nvim_create_user_command('Build', build_debug, {})

vim.api.nvim_create_user_command('BuildDebug', build_debug, {})
vim.api.nvim_create_user_command('BuildRelease', build_release, {})
vim.api.nvim_create_user_command('BuildReleaseOnline', build_release_online, {})

vim.api.nvim_create_user_command('CmakeDebug', cmake_debug, {})
vim.api.nvim_create_user_command('CmakeRelease', cmake_release, {})
vim.api.nvim_create_user_command('CmakeReleaseOnline', cmake_release_online, {})

vim.api.nvim_create_user_command('CmakeAll', function()
  cmake_debug()
  cmake_release()
  cmake_release_online()
end, {})

vim.api.nvim_create_user_command('BuildAll', function()
  build_debug()
  build_release()
  build_release_online()
end, {})


if vim.fn.has('win32') == 1 and vim.env.VCINSTALLDIR == nil then
  local vcvars = [["C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"]]
  local command = 'call ' .. vcvars .. ' && set'
  vim.fn.jobstart(command,
   {
     on_stdout = function(_, data)
       if data == nil then
         return
       end
       for _, line in pairs(data) do
         local k, v = string.match(line, "(.+)=(.+)\r")
         if k then
           vim.env[k] = v
         end

       end
     end,
     on_stderr = function(_, data)
       if (#data > 0) then
         data = table.concat(data, "\n")
         vim.notify(data, vim.log.levels.ERROR)
       end
     end,
     on_exit = function(_, code)
       if code ~= 0 then
         vim.notify('Failed to load vcvars (' .. code .. ')', vim.log.levels.ERROR)
       else
         vim.notify('Loaded vcvars', vim.log.levels.INFO)
       end
     end,
   }
  )
end

