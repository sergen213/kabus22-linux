# Apply only the recovered GenerateContact body, on every configure (not just
# FetchContent's extraction/patch stamp). Keep the official archive unchanged.
# Authority: world-contact-boundary/contract.txt section 4 and paired 100c9930 ASM.
set(_kabus_contact_file "${kabus_ode_SOURCE_DIR}/ode/src/collision_trimesh_box.cpp")
set(_kabus_contact_original_sha256 "748b26ef299b6abeb0ddf23d910889673ecf18da01d34f6c94eedb69ccc72970")
set(_kabus_contact_modified_sha256 "b4e81e712cd5c833e38d95287b4ce415bc8db32e20deacc2f9eae93b5c6ddd42")
file(SHA256 "${_kabus_contact_file}" _kabus_contact_actual_sha256)
set(_kabus_contact_replacement [=[void sTrimeshBoxColliderData::GenerateContact(int TriIndex, const dVector3 in_ContactPos, const dVector3 in_Normal, dReal in_Depth)
{
    // acknex.dll 100c9930: float storage, live x87 PC64 ordered predicates.
#if !defined(dSINGLE) || !defined(__LDBL_MANT_DIG__) || __LDBL_MANT_DIG__ != 64
#error Kabus contact merge requires float dReal and 64-significand-bit long double
#endif
    if (m_ctContacts == (m_iFlags & 0xffff))
        return;

    bool matched = false;
    for (int i = 0; i < m_ctContacts; ++i)
    {
        dContactGeom* Contact = SAFECONTACT(m_iFlags, m_ContactGeoms, i, m_iStride);
        const long double dx = (long double)in_ContactPos[0] - (long double)Contact->pos[0];
        const long double dy = (long double)in_ContactPos[1] - (long double)Contact->pos[1];
        const long double dz = (long double)in_ContactPos[2] - (long double)Contact->pos[2];
        const long double distance2 = (dy * dy + dx * dx) + dz * dz;
        if (distance2 < 1.1920928955078125e-7L) // float word 0x34000000
        {
            const long double dot =
                ((long double)in_Normal[2] * (long double)Contact->normal[2]
                 + (long double)in_Normal[0] * (long double)Contact->normal[0])
                + (long double)in_Normal[1] * (long double)Contact->normal[1];
            const long double absoluteDot = dot < 0 ? -dot : dot;
            if (absoluteDot > 0.99999988079071044921875L) // float word 0x3f7ffffe
            {
                if (in_Depth > Contact->depth)
                    Contact->depth = in_Depth;
                matched = true; // Promote every match, retaining first geometry.
            }
        }
    }
    if (matched)
        return;

    dContactGeom* Contact = SAFECONTACT(m_iFlags, m_ContactGeoms, m_ctContacts, m_iStride);
    SET(Contact->pos, in_ContactPos);
    Contact->pos[3] = 0;
    SET(Contact->normal, in_Normal);
    Contact->normal[3] = 0;
    Contact->depth = in_Depth;
    Contact->g1 = m_Geom1;
    Contact->g2 = m_Geom2;
    // Native-only compatibility metadata: original 44-byte payload has no sides.
    Contact->side1 = TriIndex;
    Contact->side2 = -1;
    ++m_ctContacts;
}]=])
if(NOT _kabus_contact_actual_sha256 STREQUAL _kabus_contact_original_sha256
   AND NOT _kabus_contact_actual_sha256 STREQUAL _kabus_contact_modified_sha256)
    message(FATAL_ERROR "Unexpected ODE contact source SHA256: ${_kabus_contact_actual_sha256}")
endif()
# Reconstruct and authenticate on both paths: changed helper contents cannot
# silently leave an old replacement in an already populated FetchContent cache.
file(READ "${_kabus_contact_file}" _kabus_contact_source)
string(FIND "${_kabus_contact_source}" "void sTrimeshBoxColliderData::GenerateContact(" _kabus_contact_start)
string(FIND "${_kabus_contact_source}" "\n\n\n\n\n\nvoid sTrimeshBoxColliderData::SetupInitialContext" _kabus_contact_end)
if(_kabus_contact_start LESS 0 OR _kabus_contact_end LESS _kabus_contact_start)
    message(FATAL_ERROR "Pinned ODE GenerateContact boundary not found")
endif()
string(SUBSTRING "${_kabus_contact_source}" 0 ${_kabus_contact_start} _kabus_contact_prefix)
string(SUBSTRING "${_kabus_contact_source}" ${_kabus_contact_end} -1 _kabus_contact_suffix)
set(_kabus_contact_source "${_kabus_contact_prefix}${_kabus_contact_replacement}${_kabus_contact_suffix}")
string(SHA256 _kabus_contact_result_sha256 "${_kabus_contact_source}")
if(NOT _kabus_contact_result_sha256 STREQUAL _kabus_contact_modified_sha256)
    message(FATAL_ERROR "ODE contact replacement recipe/hash mismatch")
endif()
if(_kabus_contact_actual_sha256 STREQUAL _kabus_contact_original_sha256)
    file(WRITE "${_kabus_contact_file}" "${_kabus_contact_source}")
endif()
message(STATUS "Kabus ODE contact source SHA256: ${_kabus_contact_modified_sha256}")
