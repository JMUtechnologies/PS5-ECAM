Read AGENTS.md and CONTRIBUTING.md at the repository root before suggesting changes; they are the
project's direction. In short: Playtendoh is one app in which every Nintendo system works the same way.
New systems are libretro cores from their official upstream repositories, pinned in tools/deps.json,
run by the app's own libretro frontend (port/libretro); no new standalone programs or personal forks.
Keep the owner's look and voice: colours, sizes, type and motion only from port/ui/tokens.json, new
options as rows on existing pages or in the in-game sheet (port/app/side_menu.h), player-facing words
plain and short like the strings around them; a new screen or layout needs the owner's yes first.
Start from the newest PS5-ECAM branch, work in a fork, rebase and squash fix-ups before a pull request.
Side counts come from ps5retro::kSystems, never hard-coded. Files through ps5files::OwnStatus. Strings
through Tr/TrF/TrP, translated in every port/lang/*.po. Build what you change (make release, or the
part's own target) and show the PC preview for anything on screen. Match the surrounding code, keep
each change to one topic, and never claim build or console results that were not run.
