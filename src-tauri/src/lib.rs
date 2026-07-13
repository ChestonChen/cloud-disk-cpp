use std::fs;
use std::sync::Mutex;

use tauri::Manager;
use tauri_plugin_shell::process::CommandChild;
use tauri_plugin_shell::ShellExt;

struct BackendProcess(Mutex<Option<CommandChild>>);

pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_shell::init())
        .setup(|app| {
            let data_dir = app.path().app_data_dir()?;
            let storage_dir = data_dir.join("storage");
            fs::create_dir_all(&storage_dir)?;

            let sidecar = app
                .shell()
                .sidecar("cloud-disk")?
                .env("CLOUD_DISK_PORT", "18080")
                .env("CLOUD_DISK_STORAGE", storage_dir);

            let (_rx, child) = sidecar.spawn()?;
            app.manage(BackendProcess(Mutex::new(Some(child))));
            Ok(())
        })
        .on_window_event(|window, event| {
            if let tauri::WindowEvent::Destroyed = event {
                if let Some(process) = window.app_handle().try_state::<BackendProcess>() {
                    if let Ok(mut child) = process.0.lock() {
                        if let Some(child) = child.take() {
                            let _ = child.kill();
                        }
                    }
                }
            }
        })
        .run(tauri::generate_context!())
        .expect("failed to run Cloud Disk desktop app");
}

