# Windows HTTP 호스트 종료 검증

Windows에서 Python의 SIGTERM 전송은 프로세스를 즉시 종료하므로 graceful drain을
검사할 수 없다. CLI는 SIGBREAK도 정상 종료 요청으로 처리한다. 실제 소켓 검사는
시험 자식을 별도 process group에 두고 CTRL_BREAK_EVENT를 해당 자식에 전달한다.
유예 중 이미 수락한 본문·응답을 완료하고 0으로 종료하는지 확인한다. POSIX는
기존 SIGTERM 검사를 유지한다.
