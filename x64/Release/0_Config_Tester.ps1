# Запуск Capturer с логированием
$Host.UI.RawUI.WindowTitle = "Config"
.\0_Config_Tester.exe | Tee-Object -FilePath "Config_test.txt"
pause