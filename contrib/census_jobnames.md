Filters applied to basenames for files in...

| run-parts | /etc/cron.$period/
| -         | -
| Debian    | `^[[:alnum:]_-]*$`
| Gentoo    | ^ (from debianutils)
| Arch      | ^ (from debianutils)
| Fedora    | not `~$`, not `,$`, not `\.rpm(save|orig|new)$`, not `\.cfsaved$`, not `\.swp$`, not `,v$`

| cron               | host       | /etc/cron.d/
| -                  | -          | -
| (vixie-)cron       | Debian     | `^[[:alnum:]_-]*$`
| cronie             | all        | not `^\.`, not `~$`, not `^#`, not `\.rpm(save|orig|new)$`, not ".cron.hostname"
| systemd-cron 2.6.0 | all        | not `^\.`, not `~`, not `\.dpkg`, not `\.rpm`, not "0anacron", not "anacron"
| bcron              | Debian     | `^[[:alnum:]_-]*$`
| bcron              | not Debian | not `\.`
| dcron              | all        | not `\.`, not "cron.update"

<https://github.com/systemd-cron/systemd-cron/issues/177#issuecomment-4252582971>
