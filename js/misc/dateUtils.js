/* exported formatDateWithCFormatString, formatTime, formatTimeSpan, clearCachedLocalTimeZone */

const System = imports.system;
const Gettext = imports.gettext;
const GLib = imports.gi.GLib;
const Gio = imports.gi.Gio;
const Shell = imports.gi.Shell;
const Params = imports.misc.params;

let _desktopSettings = null;
let _localTimeZone = null;

function formatDateWithCFormatString(date, format) {
    if (_localTimeZone === null)
        _localTimeZone = GLib.TimeZone.new_local();

    let dt = GLib.DateTime.new(_localTimeZone,
        date.getFullYear(),
        date.getMonth() + 1,
        date.getDate(),
        date.getHours(),
        date.getMinutes(),
        date.getSeconds());
    return dt?.format(format) ?? '';
}

function formatTimeSpan(date) {
    let now = GLib.DateTime.new_now_local();

    const timespan = now.difference(date);

    const minutesAgo = timespan / GLib.TIME_SPAN_MINUTE;
    const hoursAgo = timespan / GLib.TIME_SPAN_HOUR;
    const daysAgo = timespan / GLib.TIME_SPAN_DAY;
    const weeksAgo = daysAgo / 7;
    const monthsAgo = daysAgo / 30;
    const yearsAgo = weeksAgo / 52;

    if (minutesAgo < 5)
        return _('Just now');
    if (hoursAgo < 1) {
        return Gettext.ngettext(
            '%d minute ago',
            '%d minutes ago',
            minutesAgo
        ).format(minutesAgo);
    }
    if (daysAgo < 1) {
        return Gettext.ngettext(
            '%d hour ago',
            '%d hours ago',
            hoursAgo
        ).format(hoursAgo);
    }
    if (daysAgo < 2)
        return _('Yesterday');
    if (daysAgo < 15) {
        return Gettext.ngettext(
            '%d day ago',
            '%d days ago',
            daysAgo
        ).format(daysAgo);
    }
    if (weeksAgo < 8) {
        return Gettext.ngettext(
            '%d week ago',
            '%d weeks ago',
            weeksAgo
        ).format(weeksAgo);
    }
    if (yearsAgo < 1) {
        return Gettext.ngettext(
            '%d month ago',
            '%d months ago',
            monthsAgo
        ).format(monthsAgo);
    }
    return Gettext.ngettext(
        '%d year ago',
        '%d years ago',
        yearsAgo
    ).format(yearsAgo);
}

function formatTime(time, params) {
    let date;
    // HACK: The built-in Date type sucks at timezones, which we need for the
    //       world clock; it's often more convenient though, so allow either
    //       Date or GLib.DateTime as parameter
    if (time instanceof Date)
        date = GLib.DateTime.new_from_unix_local(time.getTime() / 1000);
    else
        date = time;

    const now = GLib.DateTime.new_now_local();

    const daysAgo = now.difference(date) / (24 * 60 * 60 * 1000 * 1000);

    let format;

    if (_desktopSettings == null)
        _desktopSettings = new Gio.Settings({schema_id: 'org.gnome.desktop.interface'});
    const clockFormat = _desktopSettings.get_string('clock-format');

    params = Params.parse(params, {
        timeOnly: false,
        ampm: true,
    });

    if (clockFormat === '24h') {
        // Show only the time if date is on today
        if (daysAgo < 1 || params.timeOnly)
            /* Translators: Time in 24h format */
            format = N_('%H\u2236%M');
        // Show the word "Yesterday" and time if date is on yesterday
        else if (daysAgo < 2)
            /* Translators: this is the word "Yesterday" followed by a
             time string in 24h format. i.e. "Yesterday, 14:30" */
            // xgettext:no-c-format
            format = N_('Yesterday, %H\u2236%M');
        // Show a week day and time if date is in the last week
        else if (daysAgo < 7)
            /* Translators: this is the week day name followed by a time
             string in 24h format. i.e. "Monday, 14:30" */
            // xgettext:no-c-format
            format = N_('%A, %H\u2236%M');
        else if (date.get_year() === now.get_year())
            /* Translators: this is the month name and day number
             followed by a time string in 24h format.
             i.e. "May 25, 14:30" */
            // xgettext:no-c-format
            format = N_('%B %-d, %H\u2236%M');
        else
            /* Translators: this is the month name, day number, year
             number followed by a time string in 24h format.
             i.e. "May 25 2012, 14:30" */
            // xgettext:no-c-format
            format = N_('%B %-d %Y, %H\u2236%M');
    } else {
        // Show only the time if date is on today
        if (daysAgo < 1 || params.timeOnly) // eslint-disable-line no-lonely-if
            /* Translators: Time in 12h format */
            format = N_('%l\u2236%M %p');
        // Show the word "Yesterday" and time if date is on yesterday
        else if (daysAgo < 2)
            /* Translators: this is the word "Yesterday" followed by a
             time string in 12h format. i.e. "Yesterday, 2:30 pm" */
            // xgettext:no-c-format
            format = N_('Yesterday, %l\u2236%M %p');
        // Show a week day and time if date is in the last week
        else if (daysAgo < 7)
            /* Translators: this is the week day name followed by a time
             string in 12h format. i.e. "Monday, 2:30 pm" */
            // xgettext:no-c-format
            format = N_('%A, %l\u2236%M %p');
        else if (date.get_year() === now.get_year())
            /* Translators: this is the month name and day number
             followed by a time string in 12h format.
             i.e. "May 25, 2:30 pm" */
            // xgettext:no-c-format
            format = N_('%B %-d, %l\u2236%M %p');
        else
            /* Translators: this is the month name, day number, year
             number followed by a time string in 12h format.
             i.e. "May 25 2012, 2:30 pm"*/
            // xgettext:no-c-format
            format = N_('%B %-d %Y, %l\u2236%M %p');
    }

    // Time in short 12h format, without the equivalent of "AM" or "PM"; used
    // when it is clear from the context
    if (!params.ampm)
        format = format.replace(/\s*%p/g, '');

    let formattedTime = date.format(Shell.util_translate_time_string(format));
    // prepend LTR-mark to colon/ratio to force a text direction on times
    return formattedTime.replace(/([:\u2236])/g, '\u200e$1');
}

/**
 * Update the timezone used by JavaScript Date objects and other
 * date utilities
 */
function clearCachedLocalTimeZone() {
    // SpiderMonkey caches the time zone so we must explicitly clear it
    // before we can update the calendar, see
    // https://bugzilla.gnome.org/show_bug.cgi?id=678507
    System.clearDateCaches();

    _localTimeZone = GLib.TimeZone.new_local();
}