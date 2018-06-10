import os
import sys

from django.db.backends import BaseDatabaseClient
#DB 클라이언트 관련 명령어 실행 시의 옵션
class DatabaseClient(BaseDatabaseClient):
    executable_name = 'csql'

    def runshell(self):
        settings_dict = self.connection.settings_dict
        args = [self.executable_name]
        if settings_dict['USER']:
            args += ["-u", settings_dict['USER']]
        if settings_dict['PASSWORD']:
            args += ["-p", settings_dict['PASSWORD']]
        if settings_dict['NAME'] and settings_dict['HOST']:
            args += [settings_dict['NAME'] + "@" + settings_dict['HOST']]

        if os.name == 'nt':
            sys.exit(os.system(" ".join(args)))
        else:
            os.execvp(self.executable_name, args)
